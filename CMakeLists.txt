cmake_minimum_required(VERSION 3.20)
project(FastPhotone CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
add_subdirectory(lib/SDL2)

add_subdirectory(lib/glm)

add_subdirectory(lib/spdlog)

add_subdirectory(lib/entt)

add_subdirectory(lib/VulkanMemoryAllocator)

add_library(imgui STATIC
    lib/imgui/imgui.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
    lib/imgui/backends/imgui_impl_sdl2.cpp
    lib/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    lib/imgui
    lib/imgui/backends
)
target_link_libraries(imgui PUBLIC SDL2::SDL2 Vulkan::Vulkan)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE lib/stb)

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE lib/cgltf)

add_executable(FastPhotone src/main.cpp)

target_link_libraries(FastPhotone PRIVATE
    Vulkan::Vulkan
    SDL2::SDL2main
    SDL2::SDL2
    glm::glm
    spdlog::spdlog
    EnTT::EnTT
    VulkanMemoryAllocator
    imgui
    stb
    cgltf
)