cmake_minimum_required(VERSION 3.16)
project(Convertix VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

# Recherche de Raylib en local, sinon fallback via FetchContent
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
        FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
        )
        FetchContent_MakeAvailable(raylib)
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
