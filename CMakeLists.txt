cmake_minimum_required(VERSION 3.16)

project(gb_emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(gb_emu
  src/main.cpp
  src/bus.cpp
)

target_include_directories(gb_emu PRIVATE include src)
target_compile_options(gb_emu PRIVATE -Wall -Wextra)