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

# nlohmann/json
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE lib/json/include)

# Engine source files
set(ENGINE_SOURCES
    src/Core/Log/Log.cpp
    src/Core/Time/Time.cpp
    src/Core/StringAtom/StringAtom.cpp
    src/Platform/Window/Window.cpp
    src/Platform/Input/Input.cpp
    src/Renderer/VulkanContext.cpp
    src/Renderer/Swapchain.cpp
    src/Renderer/RenderPass.cpp
    src/Renderer/ResourceManager.cpp
    src/Renderer/ShaderCache.cpp
    src/Renderer/TextureCache.cpp
    src/Renderer/Texture.cpp
    src/Renderer/MaterialRegistry.cpp
    src/Renderer/SpriteRegistry.cpp
    src/Renderer/StbImpl.cpp
    src/Renderer/Pipeline.cpp
    src/Renderer/DescriptorAllocator.cpp
    src/Renderer/Vma.cpp
    src/Renderer/VertexBuffer.cpp
    src/Renderer/GridRenderer.cpp
    src/ECS/Systems/RenderSystem.cpp
    src/Gameplay/GridMap/GridMap.cpp
    src/Gameplay/TurnManager/TurnManager.cpp
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
    nlohmann_json
)

# ── Shader compilation (GLSL → SPIR-V via glslc) ──────────────────────
# glslc ships with the Vulkan SDK. Newer CMake (3.21+) sets Vulkan_GLSLC_EXECUTABLE
# through find_package(Vulkan); fall back to manual lookup on older systems.
if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
endif()
if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Install Vulkan SDK or set VULKAN_SDK env.")
endif()
message(STATUS "Using glslc: ${Vulkan_GLSLC_EXECUTABLE}")

set(SHADER_SOURCES
    ${CMAKE_SOURCE_DIR}/assets/shaders/grid.vert
    ${CMAKE_SOURCE_DIR}/assets/shaders/grid.frag
)

set(SHADER_OUTPUTS)
foreach(SHADER ${SHADER_SOURCES})
    set(SPV "${SHADER}.spv")
    add_custom_command(
        OUTPUT  ${SPV}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER} -o ${SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER}"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS ${SPV})
endforeach()

add_custom_target(Shaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(FastPhotone Shaders)

# Copy assets folder next to the executable after build
add_custom_command(TARGET FastPhotone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:FastPhotone>/assets
    COMMENT "Copying assets to output directory"
)