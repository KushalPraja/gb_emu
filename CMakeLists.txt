cmake_minimum_required(VERSION 3.16)

project(gb_emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)

add_executable(gb_emu
  src/main.cpp
  src/cartridge.cpp
  src/ppu.cpp
  src/apu.cpp
)

target_include_directories(gb_emu PRIVATE include src ${SDL2_INCLUDE_DIRS})
target_link_libraries(gb_emu PRIVATE ${SDL2_LIBRARIES})
target_compile_options(gb_emu PRIVATE -Wall -Wextra)
