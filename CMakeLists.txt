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

# Dear ImGui (vendored) with the SDL2 + SDL_Renderer backends.
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui PUBLIC
  ${IMGUI_DIR} ${IMGUI_DIR}/backends ${SDL2_INCLUDE_DIRS})
target_link_libraries(imgui PUBLIC ${SDL2_LIBRARIES})

add_executable(gb_emu WIN32
  src/main.cpp
  src/cartridge.cpp
  src/ppu.cpp
  src/apu.cpp
  src/display.cpp
)

target_include_directories(gb_emu PRIVATE include src ${SDL2_INCLUDE_DIRS})

# On Windows, SDL renames main() to SDL_main and SDL2main provides the real
# WinMain entry point, so it must be linked when the target exists.
if(TARGET SDL2::SDL2main)
  target_link_libraries(gb_emu PRIVATE SDL2::SDL2main)
endif()

target_link_libraries(gb_emu PRIVATE ${SDL2_LIBRARIES} imgui)
target_compile_options(gb_emu PRIVATE -Wall -Wextra)
