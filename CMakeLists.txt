cmake_minimum_required(VERSION 3.16)
project(Convertix VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

if(WIN32)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
    )
    FetchContent_MakeAvailable(raylib)
else()
    find_package(raylib QUIET)
    if(NOT raylib_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(RAYLIB QUIET IMPORTED_TARGET raylib)
        endif()

        if(RAYLIB_FOUND)
            add_library(raylib ALIAS PkgConfig::RAYLIB)
        else()
            include(FetchContent)
            set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(
                raylib
                GIT_REPOSITORY https://github.com/raysan5/raylib.git
                GIT_TAG 5.5
            )
            FetchContent_MakeAvailable(raylib)
        endif()
    endif()
endif()

add_executable(convertix
    src/main.cpp
    src/FileDetector.cpp
    src/FormatMatrix.cpp
    src/Converter.cpp
    src/App.cpp
)

target_include_directories(convertix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(convertix PRIVATE
    raylib
    Threads::Threads
)

if(WIN32)
    target_compile_definitions(convertix PRIVATE
        _GNU_SOURCE
        __USE_MINGW_ANSI_STDIO=1
        UNICODE
        _UNICODE
    )

    target_link_libraries(convertix PRIVATE
        opengl32
        gdi32
        winmm
        shell32
    )

    if(MINGW)
        target_link_options(convertix PRIVATE
            -mwindows
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
endif()

set_target_properties(convertix PROPERTIES OUTPUT_NAME "Convertix")

add_custom_command(TARGET convertix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        "$<TARGET_FILE_DIR:convertix>/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:convertix>/assets"
    COMMENT "Copie des dossiers bin/ et assets/ a cote de l'executable..."
)