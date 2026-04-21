cmake_minimum_required(VERSION 3.20)
project(FastPhotone CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vulkan
find_package(Vulkan REQUIRED)

# SDL2
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
add_subdirectory(lib/SDL2)

# GLM
add_subdirectory(lib/glm)

# spdlog
add_subdirectory(lib/spdlog)

# EnTT
add_subdirectory(lib/entt)

# VulkanMemoryAllocator
add_subdirectory(lib/VulkanMemoryAllocator)

# ImGui
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

# stb
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE lib/stb)

# cgltf
add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE lib/cgltf)

# Engine source files
set(ENGINE_SOURCES
    src/Core/Log/Log.cpp
    src/Core/Time/Time.cpp
)

# Executable
add_executable(FastPhotone
    src/main.cpp
    ${ENGINE_SOURCES}
)

# Include paths
target_include_directories(FastPhotone PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/Core
)

# Debug / Release defines
target_compile_definitions(FastPhotone PRIVATE
    $<$<CONFIG:Debug>:FP_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:FP_DEBUG>
)

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