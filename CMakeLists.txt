cmake_minimum_required(VERSION 3.20)
project(toyc-compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(toyc-compiler
    src/main.cpp
    src/token.cpp
    src/lexer.cpp
)

target_include_directories(toyc-compiler PRIVATE include)

if(MSVC)
    target_compile_options(toyc-compiler PRIVATE /W4 /utf-8)
else()
    target_compile_options(toyc-compiler PRIVATE -Wall -Wextra -Wpedantic)
endif()
