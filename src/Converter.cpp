#include "Converter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
extern "C" int sinflate(void* out, int cap, const void* in, int size);
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace {

std::filesystem::path g_appBasePath;

#if defined(_WIN32)
std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    if (len <= 0) {
        codePage = CP_ACP;
        len = MultiByteToWideChar(codePage, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    }
    if (len <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(codePage, 0, str.c_str(), static_cast<int>(str.size()), result.data(), len);
    return result;
}

std::string stripShellRedirects(std::string cmd) {
    const std::array<std::string, 4> suffixes = {
        " > NUL 2>&1",
        " > /dev/null 2>&1",
        " 2>&1",
        " 2>NUL"
    };
    for (const auto& s : suffixes) {
        if (cmd.size() >= s.size() && cmd.compare(cmd.size() - s.size(), s.size(), s) == 0) {
            cmd.erase(cmd.size() - s.size());
        }
    }
    return cmd;
}

int runWin32WithLineCallback(const std::string& rawCommand, const std::function<void(const std::string&)>& onLine);

int runWin32Silent(const std::string& rawCommand) {
    return runWin32WithLineCallback(rawCommand, nullptr);
}

int runWin32WithLineCallback(const std::string& rawCommand, const std::function<void(const std::string&)>& onLine) {
    const std::string cleanCmd = stripShellRedirects(rawCommand);
    std::wstring wideCmd = utf8ToWide(cleanCmd);
    if (wideCmd.empty()) {
        return -1;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return -1;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    std::vector<wchar_t> cmdBuffer(wideCmd.begin(), wideCmd.end());
    cmdBuffer.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        nullptr,
        cmdBuffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        return -1;
    }

    std::string pending;
    std::array<char, 512> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(hRead, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
        pending.append(buffer.data(), bytesRead);
        std::size_t pos = 0;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos + 1);
            pending.erase(0, pos + 1);
            if (onLine) {
                onLine(line);
            }
        }
    }
    if (!pending.empty() && onLine) {
        onLine(pending);
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
}
#endif

std::filesystem::path detectExecutableFilePath(const char* argv0) {
    std::error_code ec;
#if defined(_WIN32)
    std::array<wchar_t, 32768> buffer{};
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len > 0 && len < buffer.size()) {
        std::filesystem::path exePath(buffer.data());
        exePath.make_preferred();
        return exePath;
    }
#else
    const std::filesystem::path procExe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !procExe.empty()) {
        const auto canonPath = std::filesystem::weakly_canonical(procExe, ec);
        return ec ? procExe : canonPath;
    }
#endif

    if (argv0 != nullptr && argv0[0] != '\0') {
        const std::filesystem::path fallbackPath = std::filesystem::absolute(std::filesystem::path(argv0), ec);
        if (!ec && !fallbackPath.empty()) {
            const auto canonFallback = std::filesystem::weakly_canonical(fallbackPath, ec);
            return ec ? fallbackPath : canonFallback;
        }
    }

    return {};
}

std::filesystem::path detectExecutableBasePath(const char* argv0) {
    const std::filesystem::path exeFile = detectExecutableFilePath(argv0);
    if (!exeFile.empty()) {
        return exeFile.parent_path();
    }
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::string getToolSubdir(const std::string& toolName) {
    if (toolName == "magick" || toolName == "identify") {
        return "imagemagick";
    }
    if (toolName == "ffprobe") {
        return "ffmpeg";
    }
    return toolName;
}

struct EmbeddedEntry {
    std::string group;
    std::string relPath;
    std::uint64_t offsetInExe{0};
    std::uint64_t compressedSize{0};
    std::uint64_t uncompressedSize{0};
};

struct EmbeddedBundle {
    bool initialized{false};
    bool valid{false};
    std::filesystem::path exePath;
    std::filesystem::path extractRoot;
    std::vector<EmbeddedEntry> entries;
};

std::mutex g_bundleMutex;
EmbeddedBundle g_embeddedBundle;

const EmbeddedBundle& getEmbeddedBundle() {
    std::lock_guard<std::mutex> lock(g_bundleMutex);
    if (g_embeddedBundle.initialized) {
        return g_embeddedBundle;
    }
    g_embeddedBundle.initialized = true;
    g_embeddedBundle.exePath = detectExecutableFilePath(nullptr);
    if (g_embeddedBundle.exePath.empty()) {
        return g_embeddedBundle;
    }

    std::error_code ec;
    const auto totalSize = std::filesystem::file_size(g_embeddedBundle.exePath, ec);
    if (ec || totalSize < 24) {
        return g_embeddedBundle;
    }

    std::ifstream file(g_embeddedBundle.exePath, std::ios::binary);
    if (!file.is_open()) {
        return g_embeddedBundle;
    }

    file.seekg(static_cast<std::streamoff>(totalSize - 24), std::ios::beg);
    char magic[8]{};
    std::uint64_t tocOffset = 0;
    std::uint64_t bundleHash = 0;
    file.read(magic, 8);
    file.read(reinterpret_cast<char*>(&tocOffset), sizeof(tocOffset));
    file.read(reinterpret_cast<char*>(&bundleHash), sizeof(bundleHash));

    if (!file || std::memcmp(magic, "CVTXPK01", 8) != 0 || tocOffset >= totalSize - 24) {
        return g_embeddedBundle;
    }

    file.seekg(static_cast<std::streamoff>(tocOffset), std::ios::beg);
    std::uint32_t entryCount = 0;
    file.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));
    if (!file || entryCount == 0 || entryCount > 256) {
        return g_embeddedBundle;
    }

    g_embeddedBundle.entries.reserve(entryCount);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        std::uint32_t grpLen = 0;
        file.read(reinterpret_cast<char*>(&grpLen), sizeof(grpLen));
        if (!file || grpLen == 0 || grpLen > 256) {
            return g_embeddedBundle;
        }
        std::string group(grpLen, '\0');
        file.read(group.data(), static_cast<std::streamsize>(grpLen));

        std::uint32_t pathLen = 0;
        file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        if (!file || pathLen == 0 || pathLen > 1024) {
            return g_embeddedBundle;
        }
        std::string relPath(pathLen, '\0');
        file.read(relPath.data(), static_cast<std::streamsize>(pathLen));

        std::uint64_t offsetInExe = 0;
        std::uint64_t compressedSize = 0;
        std::uint64_t uncompressedSize = 0;
        file.read(reinterpret_cast<char*>(&offsetInExe), sizeof(offsetInExe));
        file.read(reinterpret_cast<char*>(&compressedSize), sizeof(compressedSize));
        file.read(reinterpret_cast<char*>(&uncompressedSize), sizeof(uncompressedSize));
        if (!file) {
            return g_embeddedBundle;
        }

        g_embeddedBundle.entries.push_back(EmbeddedEntry{
            std::move(group),
            std::move(relPath),
            offsetInExe,
            compressedSize,
            uncompressedSize
        });
    }

    std::ostringstream hashHex;
    hashHex << "Convertix_Runtime_" << std::hex << std::setw(16) << std::setfill('0') << bundleHash;
    g_embeddedBundle.extractRoot = std::filesystem::temp_directory_path(ec) / hashHex.str();
    g_embeddedBundle.valid = true;
    return g_embeddedBundle;
}

bool hasToolInEmbeddedBundle(const std::string& toolName, const std::string& exeFile) {
    const EmbeddedBundle& bundle = getEmbeddedBundle();
    if (!bundle.valid) {
        return false;
    }
    const std::string subDir = getToolSubdir(toolName);
    const std::string expectedSuffix = "/" + exeFile;
    for (const auto& entry : bundle.entries) {
        if (entry.group == subDir &&
            entry.relPath.size() >= expectedSuffix.size() &&
            entry.relPath.compare(entry.relPath.size() - expectedSuffix.size(), expectedSuffix.size(), expectedSuffix) == 0) {
            return true;
        }
    }
    return false;
}

std::string extractToolFromEmbeddedBundle(const std::string& toolName, const std::string& exeFile) {
    const EmbeddedBundle& bundle = getEmbeddedBundle();
    if (!bundle.valid) {
        return {};
    }

    static std::mutex s_extractMutex;
    std::lock_guard<std::mutex> lock(s_extractMutex);

    const std::string subDir = getToolSubdir(toolName);
    const std::string expectedSuffix = "/" + exeFile;
    std::filesystem::path resolvedExePath;
    std::error_code ec;

    std::ifstream exeStream;
    for (const auto& entry : bundle.entries) {
        if (entry.group != subDir) {
            continue;
        }

        std::filesystem::path targetPath = bundle.extractRoot / entry.relPath;
        targetPath.make_preferred();

        const bool alreadyExtracted =
            std::filesystem::exists(targetPath, ec) &&
            std::filesystem::is_regular_file(targetPath, ec) &&
            std::filesystem::file_size(targetPath, ec) == entry.uncompressedSize;

        if (!alreadyExtracted) {
            std::filesystem::create_directories(targetPath.parent_path(), ec);
            if (!exeStream.is_open()) {
                exeStream.open(bundle.exePath, std::ios::binary);
                if (!exeStream.is_open()) {
                    return {};
                }
            }

            std::vector<unsigned char> compBuf(static_cast<std::size_t>(entry.compressedSize));
            exeStream.seekg(static_cast<std::streamoff>(entry.offsetInExe), std::ios::beg);
            exeStream.read(reinterpret_cast<char*>(compBuf.data()), static_cast<std::streamsize>(entry.compressedSize));
            if (!exeStream) {
                return {};
            }

#if defined(_WIN32)
            std::vector<unsigned char> uncompBuf(static_cast<std::size_t>(entry.uncompressedSize) + 64, 0);
            const int decompLen = sinflate(
                uncompBuf.data(),
                static_cast<int>(uncompBuf.size()),
                compBuf.data(),
                static_cast<int>(compBuf.size())
            );
            if (decompLen < 0 || static_cast<std::uint64_t>(decompLen) != entry.uncompressedSize) {
                return {};
            }

            std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                return {};
            }
            out.write(reinterpret_cast<const char*>(uncompBuf.data()), static_cast<std::streamsize>(entry.uncompressedSize));
            out.close();
#else
            return {};
#endif
        }

        if (entry.relPath.size() >= expectedSuffix.size() &&
            entry.relPath.compare(entry.relPath.size() - expectedSuffix.size(), expectedSuffix.size(), expectedSuffix) == 0) {
            resolvedExePath = targetPath;
        }
    }

    if (!resolvedExePath.empty() && std::filesystem::exists(resolvedExePath, ec)) {
        return "\"" + resolvedExePath.string() + "\"";
    }
    return {};
}

int runProcessWithLines(const std::string& command, const std::function<void(const std::string&)>& onLine) {
#if defined(_WIN32)
    return runWin32WithLineCallback(command, onLine);
#else
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return -1;
    }
    std::array<char, 512> lineBuf{};
    while (std::fgets(lineBuf.data(), static_cast<int>(lineBuf.size()), pipe) != nullptr) {
        if (onLine) {
            onLine(std::string(lineBuf.data()));
        }
    }
    return pclose(pipe);
#endif
}

// Parse une chaîne "HH:MM:SS.xx" en secondes
double parseHMS(const std::string& hms) {
    int h = 0;
    int m = 0;
    double s = 0.0;
    if (std::sscanf(hms.c_str(), "%d:%d:%lf", &h, &m, &s) == 3) {
        return static_cast<double>(h) * 3600.0 + static_cast<double>(m) * 60.0 + s;
    }
    return 0.0;
}

// Sélectionne la couche de plus haute résolution dans un fichier .ico (fallback sur [-1])
std::string resolveBestIcoInputSpec(const std::filesystem::path& icoPath, const std::string& magickCmd) {
    const std::string identifyCmd =
        magickCmd + " identify -ping -format \"%s %w %h\\n\" \"" + icoPath.string() + "\" 2>&1";

    int bestScene = -1;
    long long bestArea = -1;

    runProcessWithLines(identifyCmd, [&](const std::string& line) {
        int scene = -1;
        int w = 0;
        int h = 0;
        if (std::sscanf(line.c_str(), "%d %d %d", &scene, &w, &h) == 3 && scene >= 0 && w > 0 && h > 0) {
            const long long area = static_cast<long long>(w) * static_cast<long long>(h);
            if (area >= bestArea) {
                bestArea = area;
                bestScene = scene;
            }
        }
    });

    if (bestScene >= 0) {
        return icoPath.string() + "[" + std::to_string(bestScene) + "]";
    }

    return icoPath.string() + "[-1]";
}

std::filesystem::path buildUniqueOutputPath(const std::filesystem::path& input, const std::string& ext) {
    const std::filesystem::path parentDir = input.parent_path();
    const std::string stem = input.stem().string();
    std::error_code ec;

    std::filesystem::path candidate = parentDir / (stem + "_cix" + ext);
    if (!std::filesystem::exists(candidate, ec)) {
        return candidate;
    }

    for (int index = 1; index < 10000; ++index) {
        candidate = parentDir / (stem + "_cix_" + std::to_string(index) + ext);
        if (!std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return candidate;
}

} // namespace

Converter::Converter() {
    checkToolsAvailability();
}

Converter::~Converter() {
    if (m_task.valid()) {
        m_task.wait();
    }
}

void Converter::initBasePath(const char* argv0) {
    g_appBasePath = detectExecutableBasePath(argv0);
}

const std::filesystem::path& Converter::getBasePath() {
    if (g_appBasePath.empty()) {
        g_appBasePath = detectExecutableBasePath(nullptr);
    }
    return g_appBasePath;
}

int Converter::executeCommandSilent(const std::string& command) {
#if defined(_WIN32)
    return runWin32Silent(command);
#else
    return std::system(command.c_str());
#endif
}

std::string Converter::executeCommandCapture(const std::string& command) {
    std::string output;
    runProcessWithLines(command, [&](const std::string& line) {
        output += line;
    });
    return output;
}

std::optional<std::filesystem::path> Converter::openNativeFilePicker() {
#if defined(_WIN32)
    std::array<wchar_t, 4096> fileBuffer{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = fileBuffer.data();
    ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    ofn.lpstrFilter =
        L"Fichiers support\u00e9s\0*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.gif;*.heic;*.tiff;*.tif;*.ico;*.svg;*.dng;*.cr2;*.nef;*.arw;*.mp3;*.wav;*.flac;*.ogg;*.aac;*.m4a;*.mp4;*.mkv;*.avi;*.mov;*.webm;*.docx;*.odt;*.md;*.html;*.htm;*.txt\0"
        L"Images\0*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.gif;*.heic;*.tiff;*.tif;*.ico;*.svg;*.dng;*.cr2;*.nef;*.arw\0"
        L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.aac;*.m4a\0"
        L"Vid\u00e9o\0*.mp4;*.mkv;*.avi;*.mov;*.webm\0"
        L"Documents\0*.docx;*.odt;*.md;*.html;*.htm;*.txt\0"
        L"Tous les fichiers (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"S\u00e9lectionner un fichier";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn) != 0 && fileBuffer[0] != L'\0') {
        return std::filesystem::path(fileBuffer.data());
    }
    return std::nullopt;
#else
    const char* cmd =
        "if command -v zenity >/dev/null 2>&1; then "
        "zenity --file-selection --title=\"Sélectionner un fichier\" "
        "--file-filter=\"Fichiers supportés (Image, Audio, Vidéo, Document) | *.png *.PNG *.jpg *.JPG *.jpeg *.JPEG *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.heic *.HEIC *.heif *.HEIF *.tiff *.TIFF *.tif *.TIF *.ico *.ICO *.svg *.SVG *.dng *.DNG *.cr2 *.CR2 *.nef *.NEF *.arw *.ARW *.mp3 *.MP3 *.wav *.WAV *.flac *.FLAC *.ogg *.OGG *.aac *.AAC *.m4a *.M4A *.mp4 *.MP4 *.mkv *.MKV *.avi *.AVI *.mov *.MOV *.webm *.WEBM *.docx *.DOCX *.odt *.ODT *.md *.MD *.html *.HTML *.htm *.HTM *.txt *.TXT\" "
        "--file-filter=\"Images | *.png *.PNG *.jpg *.JPG *.jpeg *.JPEG *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.heic *.HEIC *.tiff *.TIFF *.tif *.TIF *.ico *.ICO *.svg *.SVG *.dng *.DNG *.cr2 *.CR2 *.nef *.NEF *.arw *.ARW\" "
        "--file-filter=\"Audio | *.mp3 *.MP3 *.wav *.WAV *.flac *.FLAC *.ogg *.OGG *.aac *.AAC *.m4a *.M4A\" "
        "--file-filter=\"Vidéo | *.mp4 *.MP4 *.mkv *.MKV *.avi *.AVI *.mov *.MOV *.webm *.WEBM\" "
        "--file-filter=\"Documents | *.docx *.DOCX *.odt *.ODT *.md *.MD *.html *.HTML *.htm *.HTM *.txt *.TXT\" "
        "--file-filter=\"Tous les fichiers | *\" 2>/dev/null; "
        "elif command -v kdialog >/dev/null 2>&1; then "
        "kdialog --getopenfilename . \"*.png *.jpg *.jpeg *.webp *.bmp *.gif *.heic *.tiff *.tif *.ico *.svg *.dng *.cr2 *.nef *.arw *.mp3 *.wav *.flac *.ogg *.aac *.m4a *.mp4 *.mkv *.avi *.mov *.webm *.docx *.odt *.md *.html *.htm *.txt|Fichiers supportés\" 2>/dev/null; "
        "elif command -v yad >/dev/null 2>&1; then "
        "yad --file --title=\"Sélectionner un fichier\" 2>/dev/null; "
        "fi";
    std::string output = executeCommandCapture(cmd);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    if (output.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(output);
#endif
}

std::string Converter::resolveToolPath(const std::string& toolName) {
    const std::string subDir = getToolSubdir(toolName);
    const std::filesystem::path& basePath = getBasePath();
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);

#if defined(_WIN32)
    const std::string exeFile = toolName + ".exe";
#else
    const std::string exeFile = toolName;
#endif

    const std::array<std::filesystem::path, 8> localCandidates = {
        basePath / "bin" / subDir / exeFile,
        basePath / "bin" / exeFile,
        basePath / subDir / exeFile,
        basePath / exeFile,
        basePath.parent_path() / "bin" / subDir / exeFile,
        basePath.parent_path() / "bin" / exeFile,
        cwd / "bin" / subDir / exeFile,
        cwd / "bin" / exeFile
    };

    for (const auto& candidate : localCandidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
            std::filesystem::path normPath = std::filesystem::weakly_canonical(candidate, ec);
            if (ec || normPath.empty()) {
                normPath = std::filesystem::absolute(candidate, ec);
                if (ec || normPath.empty()) {
                    normPath = candidate;
                }
            }
            normPath.make_preferred();
            return "\"" + normPath.string() + "\"";
        }
    }

    const std::string embeddedResolved = extractToolFromEmbeddedBundle(toolName, exeFile);
    if (!embeddedResolved.empty()) {
        return embeddedResolved;
    }

    return toolName;
}

bool Converter::isToolAvailable(const std::string& toolName) {
#if defined(_WIN32)
    const std::string exeNameForCheck = toolName + ".exe";
#else
    const std::string exeNameForCheck = toolName;
#endif
    if (hasToolInEmbeddedBundle(toolName, exeNameForCheck)) {
        return true;
    }

    const std::string resolved = resolveToolPath(toolName);
    if (!resolved.empty() && resolved.front() == '"') {
        return true;
    }

    if (const char* pathEnv = std::getenv("PATH")) {
        std::stringstream ss(pathEnv);
        std::string dir;
#if defined(_WIN32)
        const char delimiter = ';';
        const std::string exeName = toolName + ".exe";
#else
        const char delimiter = ':';
        const std::string exeName = toolName;
#endif
        std::error_code ec;
        while (std::getline(ss, dir, delimiter)) {
            if (!dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / exeName, ec)) {
                return true;
            }
        }
    }
    return false;
}

double Converter::probeMediaDurationSeconds(const std::filesystem::path& input) {
    if (!isToolAvailable("ffprobe")) {
        return 0.0;
    }

    const std::string ffprobeCmd = resolveToolPath("ffprobe") +
        " -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" +
        input.string() + "\" 2>&1";

    const std::string output = executeCommandCapture(ffprobeCmd);
    try {
        const double sec = std::stod(output);
        return (sec > 0.0) ? sec : 0.0;
    } catch (...) {
        return 0.0;
    }
}

bool Converter::checkToolsAvailability() {
    m_hasMagick = isToolAvailable("magick");
    m_hasFfmpeg = isToolAvailable("ffmpeg");
    m_hasPandoc = isToolAvailable("pandoc");
    m_hasVTracer = isToolAvailable("vtracer");
    return m_hasMagick || m_hasFfmpeg || m_hasPandoc || m_hasVTracer;
}

bool Converter::isMagickAvailable() const {
    return m_hasMagick;
}

bool Converter::isFFmpegAvailable() const {
    return m_hasFfmpeg;
}

bool Converter::isFfmpegAvailable() const {
    return m_hasFfmpeg;
}

bool Converter::isPandocAvailable() const {
    return m_hasPandoc;
}

bool Converter::isVTracerAvailable() const {
    return m_hasVTracer;
}

void Converter::startConversion(const std::filesystem::path& input, FileType targetType) {
    startConversion(input, FileDetector::detect(input), targetType);
}

void Converter::startConversion(const std::filesystem::path& input, FileType sourceType, FileType targetType) {
    if (m_status.load() == ConversionStatus::IN_PROGRESS) {
        return;
    }

    if (m_task.valid()) {
        m_task.wait();
    }

    m_progress.store(0.0f);
    checkToolsAvailability();

    const std::string ext = FileDetector::toExtension(targetType);
    if (ext.empty()) {
        m_status.store(ConversionStatus::ERROR_CLI);
        return;
    }

    const FileCategory sourceCat = FileDetector::getCategory(sourceType);
    const FileCategory targetCat = FileDetector::getCategory(targetType);

    const bool useVTracerSvg = (sourceCat == FileCategory::IMAGE && targetType == FileType::SVG);
    const bool needsTempPngForVTracer =
        useVTracerSvg && (sourceType != FileType::PNG && sourceType != FileType::JPEG);
    const bool useFfmpeg = (!useVTracerSvg &&
                            (sourceCat == FileCategory::AUDIO || sourceCat == FileCategory::VIDEO ||
                             targetCat == FileCategory::AUDIO || targetCat == FileCategory::VIDEO));
    const bool usePandoc = (!useFfmpeg && !useVTracerSvg &&
                            sourceCat == FileCategory::DOCUMENT && targetCat == FileCategory::DOCUMENT);
    const bool useMagick = (!useVTracerSvg && !useFfmpeg && !usePandoc);

    if ((useVTracerSvg && (!isVTracerAvailable() || (needsTempPngForVTracer && !isMagickAvailable()))) ||
        (useFfmpeg && !isFFmpegAvailable()) ||
        (useMagick && !isMagickAvailable()) ||
        (usePandoc && !isPandocAvailable())) {
        m_status.store(ConversionStatus::MISSING_TOOL);
        return;
    }

    const std::filesystem::path outputPath = buildUniqueOutputPath(input, ext);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastOutputPath = outputPath;
    }

    m_status.store(ConversionStatus::IN_PROGRESS);

    m_task = std::async(std::launch::async, [this, input, outputPath, sourceType, sourceCat, targetCat, targetType, ext, useVTracerSvg, needsTempPngForVTracer, useFfmpeg, usePandoc]() {
#if defined(_WIN32)
        const std::string silentRedirect = "";
#else
        const std::string silentRedirect = " > /dev/null 2>&1";
#endif

        if (useVTracerSvg) {
            std::error_code ec;
            std::filesystem::path vtracerInput = input;
            std::filesystem::path tempPng;

            if (needsTempPngForVTracer) {
                tempPng = std::filesystem::temp_directory_path(ec) / "temp_vtracer_input.png";
                const std::string magickCmd = resolveToolPath("magick");
                const std::string inputSpec = (sourceType == FileType::ICO)
                    ? resolveBestIcoInputSpec(input, magickCmd)
                    : input.string();
                const std::string prepCmd = magickCmd + " \"" + inputSpec +
                                            "\" -colorspace sRGB -define png:color-type=6 -alpha on \"" +
                                            tempPng.string() + "\"" + silentRedirect;
                const int prepRes = executeCommandSilent(prepCmd);
                if (prepRes != 0 || !std::filesystem::exists(tempPng, ec)) {
                    std::filesystem::remove(tempPng, ec);
                    m_status.store(ConversionStatus::ERROR_CLI);
                    return;
                }
                vtracerInput = tempPng;
            }

            const std::string vtracerCmd = resolveToolPath("vtracer");
            // Compatibilité vtracer 0.6.x (Linux) et vtracer 1.0.0-alpha.4 (Windows) :
            // Le mode couleur est le comportement par défaut dans les deux versions,
            // tandis que l'option --colormode a été supprimée dans vtracer 1.0.0-alpha.4.
            const std::string command = vtracerCmd +
                                        " --input \"" + vtracerInput.string() +
                                        "\" --output \"" + outputPath.string() +
                                        "\" --hierarchical stacked" +
                                        silentRedirect;
            int result = executeCommandSilent(command);

            // Fallback via ImageMagick si un PNG/JPG non standard échoue directement dans VTracer
            if ((result != 0 || !std::filesystem::exists(outputPath, ec)) && tempPng.empty()) {
                tempPng = std::filesystem::temp_directory_path(ec) / "temp_vtracer_input.png";
                const std::string magickCmd = resolveToolPath("magick");
                const std::string prepCmd = magickCmd + " \"" + input.string() +
                                            "\" -colorspace sRGB -define png:color-type=6 -alpha on \"" +
                                            tempPng.string() + "\"" + silentRedirect;
                if (executeCommandSilent(prepCmd) == 0 && std::filesystem::exists(tempPng, ec)) {
                    const std::string retryCmd = vtracerCmd +
                                                 " --input \"" + tempPng.string() +
                                                 "\" --output \"" + outputPath.string() +
                                                 "\" --hierarchical stacked" +
                                                 silentRedirect;
                    result = executeCommandSilent(retryCmd);
                }
            }

            if (!tempPng.empty()) {
                std::filesystem::remove(tempPng, ec);
            }

            if (result == 0 && std::filesystem::exists(outputPath, ec)) {
                m_progress.store(100.0f);
                m_status.store(ConversionStatus::SUCCESS);
            } else {
                m_status.store(ConversionStatus::ERROR_CLI);
            }
            return;
        }

        if (useFfmpeg) {
            double totalDurationSec = probeMediaDurationSeconds(input);
            const std::string ffmpegCmd = resolveToolPath("ffmpeg");
            std::string encodeArgs;

            if (sourceCat == FileCategory::VIDEO && targetCat == FileCategory::VIDEO) {
                if (targetType == FileType::WEBM || targetType == FileType::AVI) {
                    encodeArgs = "";
                } else {
                    encodeArgs = " -c:v libx264 -crf 23 -preset medium -c:a aac -b:a 192k";
                }
            } else if (sourceCat == FileCategory::VIDEO && targetCat == FileCategory::AUDIO) {
                encodeArgs = " -vn -q:a 2";
            }

            const std::string command =
                ffmpegCmd + " -y -i \"" + input.string() + "\"" + encodeArgs +
                " -stats_period 0.05 -progress pipe:1 -nostats \"" + outputPath.string() + "\" 2>&1";

            const int exitCode = runProcessWithLines(command, [&](const std::string& line) {
                // Fallback d'extraction de durée depuis l'en-tête stderr de ffmpeg si ffprobe n'était pas disponible
                if (totalDurationSec <= 0.0) {
                    const auto durPos = line.find("Duration:");
                    if (durPos != std::string::npos) {
                        const std::string durSub = line.substr(durPos + 9);
                        const auto commaPos = durSub.find(',');
                        if (commaPos != std::string::npos) {
                            std::string hms = durSub.substr(0, commaPos);
                            hms.erase(0, hms.find_first_not_of(" \t"));
                            totalDurationSec = parseHMS(hms);
                        }
                    }
                }

                if (totalDurationSec > 0.0) {
                    if (line.rfind("out_time_us=", 0) == 0 || line.rfind("out_time_ms=", 0) == 0) {
                        try {
                            const long long currentUs = std::stoll(line.substr(12));
                            if (currentUs > 0) {
                                const double currentSec = static_cast<double>(currentUs) / 1000000.0;
                                const float pct = static_cast<float>((currentSec / totalDurationSec) * 100.0);
                                m_progress.store(std::clamp(pct, 0.0f, 99.0f));
                            }
                        } catch (...) {}
                    } else if (line.rfind("out_time=", 0) == 0) {
                        const double currentSec = parseHMS(line.substr(9));
                        if (currentSec > 0.0) {
                            const float pct = static_cast<float>((currentSec / totalDurationSec) * 100.0);
                            m_progress.store(std::clamp(pct, 0.0f, 99.0f));
                        }
                    }
                }

                if (line.rfind("progress=end", 0) == 0) {
                    m_progress.store(100.0f);
                }
            });

            std::error_code ec;
            if (exitCode == 0 && std::filesystem::exists(outputPath, ec)) {
                m_progress.store(100.0f);
                m_status.store(ConversionStatus::SUCCESS);
            } else {
                m_status.store(ConversionStatus::ERROR_CLI);
            }
            return;
        }

        std::string command;
        if (usePandoc) {
            const std::string pandocCmd = resolveToolPath("pandoc");
            command = pandocCmd + " -s \"" + input.string() + "\" -o \"" +
                      outputPath.string() + "\"" + silentRedirect;
        } else {
            const std::string magickCmd = resolveToolPath("magick");
            std::string preInputOptions;
            if (sourceType == FileType::SVG &&
                (targetType == FileType::PNG || targetType == FileType::WEBP ||
                 targetType == FileType::TIFF || targetType == FileType::ICO)) {
                preInputOptions = " -background none";
            }

            const std::string inputSpec = (sourceType == FileType::ICO)
                ? resolveBestIcoInputSpec(input, magickCmd)
                : input.string();

            std::string postInputOptions;
            if (targetType == FileType::JPEG || ext == ".jpg" || ext == ".jpeg") {
                postInputOptions = " -background white -alpha remove -alpha off";
            } else if (targetType == FileType::ICO || ext == ".ico") {
                postInputOptions = " -define icon:auto-resize=256,128,64,48,32,16";
            }
            command = magickCmd + preInputOptions + " \"" + inputSpec + "\"" + postInputOptions +
                      " \"" + outputPath.string() + "\"" + silentRedirect;
        }

        const int result = executeCommandSilent(command);

        std::error_code ec;
        if (result == 0 && std::filesystem::exists(outputPath, ec)) {
            m_progress.store(100.0f);
            m_status.store(ConversionStatus::SUCCESS);
        } else {
            m_status.store(ConversionStatus::ERROR_CLI);
        }
    });
}

ConversionStatus Converter::getStatus() const {
    return m_status.load();
}

float Converter::getProgress() const {
    return m_progress.load();
}

std::filesystem::path Converter::getLastOutputPath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastOutputPath;
}

void Converter::reset() {
    if (m_task.valid()) {
        m_task.wait();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastOutputPath.clear();
    }
    m_progress.store(0.0f);
    m_status.store(ConversionStatus::IDLE);
}
