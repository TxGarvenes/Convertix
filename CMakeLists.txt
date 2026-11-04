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

add_executable(convertix
    src/main.cpp
    src/FileDetector.cpp
    src/FormatMatrix.cpp
    src/Converter.cpp
)

target_include_directories(convertix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(convertix PRIVATE
    Threads::Threads
)
