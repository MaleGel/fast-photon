cmake_minimum_required(VERSION 3.20)
project(FastPhotone CXX C)

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

# miniaudio (single-header)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE lib/miniaudio)

# Lua 5.4 — vendored source. We compile every .c in lib/lua/ as one static
# library; the standalone 'lua' / 'luac' interpreter mains were stripped
# before vendoring so there's no main() conflict.
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS lib/lua/*.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC lib/lua)
# Lua leans on a couple of POSIX-isms (popen, dlfcn). Disable that path on
# MSVC so the build doesn't pull in os.execute / package.loadlib backends
# we don't want anyway.
if(MSVC)
    target_compile_definitions(lua PUBLIC LUA_USE_C89)
endif()

# sol3 — single-header C++ wrapper around the Lua C API.
# Include root is 'lib/' so '#include <sol/sol.hpp>' resolves to the file
# at lib/sol/sol.hpp.
add_library(sol2 INTERFACE)
target_include_directories(sol2 INTERFACE lib)
target_link_libraries(sol2 INTERFACE lua)

# Engine source files — generic, game-agnostic.
set(ENGINE_SOURCES
    src/engine/Core/Log/Log.cpp
    src/engine/Core/Time/Time.cpp
    src/engine/Core/StringAtom/StringAtom.cpp
    src/engine/Core/Events/EventBus.cpp
    src/engine/Core/StringUtil/Utf8.cpp
    src/engine/Core/Profiler/Profiler.cpp
    src/engine/Core/Profiler/ProfilerWindow.cpp
    src/engine/Core/Profiler/GpuProfiler.cpp
    src/engine/Core/Profiler/GpuProfilerWindow.cpp
    src/engine/Core/Job/JobSystem.cpp
    src/engine/App/AppController.cpp
    src/engine/App/GameLoop.cpp
    src/engine/Platform/SdlContext.cpp
    src/engine/Platform/Window/Window.cpp
    src/engine/Platform/Input/Input.cpp
    src/engine/Platform/Input/InputMap.cpp
    src/engine/Platform/FileWatcher.cpp
    src/engine/Audio/AudioImpl.cpp
    src/engine/Audio/AudioDevice.cpp
    src/engine/Audio/SoundCache.cpp
    src/engine/Audio/AudioConfig.cpp
    src/engine/Renderer/VulkanContext.cpp
    src/engine/Renderer/Swapchain.cpp
    src/engine/Renderer/RenderPass.cpp
    src/engine/Renderer/SwapchainRenderPass.cpp
    src/engine/Renderer/HdrTarget.cpp
    src/engine/Renderer/BrightImage.cpp
    src/engine/Renderer/ComputePipeline.cpp
    src/engine/Renderer/BrightExtractPass.cpp
    src/engine/Renderer/PostProcess.cpp
    src/engine/Renderer/ResourceManager.cpp
    src/engine/Renderer/ShaderCache.cpp
    src/engine/Renderer/TextureCache.cpp
    src/engine/Renderer/Texture.cpp
    src/engine/Renderer/MaterialRegistry.cpp
    src/engine/Renderer/SpriteRegistry.cpp
    src/engine/Renderer/StbImpl.cpp
    src/engine/Renderer/Pipeline.cpp
    src/engine/Renderer/DescriptorAllocator.cpp
    src/engine/Renderer/RenderQueue.cpp
    src/engine/Renderer/FrameRenderer.cpp
    src/engine/Renderer/ImGuiLayer.cpp
    src/engine/Renderer/Vma.cpp
    src/engine/Renderer/VertexBuffer.cpp
    src/engine/Renderer/UniformBuffer.cpp
    src/engine/Renderer/SpriteRenderer.cpp
    src/engine/Renderer/DebugDraw.cpp
    src/engine/Renderer/FontRegistry.cpp
    src/engine/Renderer/FontAtlasBuilder.cpp
    src/engine/Renderer/TextRenderer.cpp
    src/engine/Scene/Components/Camera/CameraConfig.cpp
    src/engine/Scene/PrefabRegistry.cpp
    src/engine/Animation/AnimationRegistry.cpp
    src/engine/Animation/AnimationSystem.cpp
    src/engine/Script/ScriptSystem.cpp
    src/engine/Vfx/ParticleSystemRegistry.cpp
    src/engine/Vfx/VfxSystem.cpp
)

# Game source files
set(GAME_SOURCES
    src/game/Gameplay/GridMap/GridMap.cpp
    src/game/Gameplay/TurnManager/TurnManager.cpp
    src/game/Renderer/GridRenderer.cpp
    src/game/Scene/World.cpp
    src/game/Scene/JsonSceneLoader.cpp
    src/game/Systems/CameraController.cpp
    src/game/Systems/RenderSystem.cpp
    src/game/Systems/SelectionSystem.cpp
)

# Executable
add_executable(FastPhotone
    src/game/main.cpp
    ${ENGINE_SOURCES}
    ${GAME_SOURCES}
)

# Include roots: engine and game are addressed relative to these paths, so
# existing #include "Core/Log/Log.h" / "Gameplay/GridMap/GridMap.h" keep working.
target_include_directories(FastPhotone PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/game
)

# Debug / Release defines
target_compile_definitions(FastPhotone PRIVATE
    $<$<CONFIG:Debug>:FP_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:FP_DEBUG>
    GLM_FORCE_DEPTH_ZERO_TO_ONE   # Vulkan clip space: z in [0,1]
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
    miniaudio
    sol2
)

# Shaders are compiled at runtime by ShaderCache (via $VULKAN_SDK/bin/glslc).
# This keeps hot-reload and startup on a single codepath — no build-time
# .spv generation needed.

# ── Manifest baker ────────────────────────────────────────────────────
# Consolidates per-faction assets/data/factions/*/assets.json on top of
# the common assets/assets.json into assets/assets.baked.json (which the
# engine reads at startup). Runs PRE_BUILD so the copy step below picks
# up a fresh baked file. We also fail the build on bake validation
# errors — a dangling reference in the source manifests is cheaper to
# diagnose here than at runtime.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET FastPhotone PRE_BUILD
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/manifest_baker/bake.py
            ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Baking asset manifest"
)

# Copy assets folder next to the executable after build
add_custom_command(TARGET FastPhotone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:FastPhotone>/assets
    COMMENT "Copying assets to output directory"
)