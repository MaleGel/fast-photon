#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>
#include <entt/entt.hpp>
#include <imgui.h>

#include <array>
#include <cmath>

#include "Core/Log/Log.h"
#include "Core/Time/Time.h"
#include "Core/Events/EventBus.h"
#include "Core/Profiler/ProfilerWindow.h"
#include "Core/Profiler/GpuProfiler.h"
#include "Core/Profiler/GpuProfilerWindow.h"
#include "App/AppController.h"
#include "App/GameLoop.h"
#include "Platform/SdlContext.h"
#include "Platform/Window/Window.h"
#include "Platform/Window/WindowEvents.h"
#include "Platform/Input/Input.h"
#include "Platform/Input/InputMap.h"
#include "Platform/FileWatcher.h"
#include "Audio/AudioDevice.h"
#include "Audio/AudioConfig.h"
#include "Audio/Sound.h"
#include "Renderer/VulkanContext.h"
#include "Renderer/Swapchain.h"
#include "Renderer/RenderPass.h"
#include "Renderer/SwapchainRenderPass.h"
#include "Renderer/HdrTarget.h"
#include "Renderer/BrightImage.h"
#include "Renderer/BrightExtractPass.h"
#include "Renderer/PostProcess.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/ShaderCache.h"
#include "Renderer/DescriptorAllocator.h"
#include "Renderer/GridRenderer.h"
#include "Renderer/SpriteRenderer.h"
#include "Renderer/TextRenderer.h"
#include "Renderer/DebugDraw.h"
#include "Systems/SelectionSystem.h"
#include "Animation/AnimationSystem.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/FrameRenderer.h"
#include "Renderer/RendererEvents.h"
#include "Renderer/ImGuiLayer.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/Camera/CameraComponent2D.h"
#include "Scene/Components/Camera/ActiveCameraTag.h"
#include "Scene/Components/Camera/CameraMath.h"
#include "Scene/World.h"
#include "Scene/JsonSceneLoader.h"

int main(int argc, char* argv[]) {
    engine::Log::init();
    (void)argc; (void)argv;
    FP_CORE_INFO("=== fast-photon starting ===");

    engine::SdlContext    sdl;
    engine::EventBus      eventBus;
    engine::InputMap      inputMap;
    inputMap.init(eventBus);
    inputMap.loadBindings("assets/data/input_bindings.json");

    engine::AppController appController;
    appController.init(eventBus);

    engine::Window              window({ "fast-photon", 1280, 720 });
    engine::VulkanContext       vkCtx;
    engine::Swapchain           swapchain;
    engine::HdrTarget           hdrTarget;
    engine::BrightImage         brightImage;
    engine::RenderPass          sceneRenderPass;
    engine::SwapchainRenderPass swapRenderPass;

    vkCtx.init(window.getNativeHandle());
    swapchain.init(vkCtx, window.getWidth(), window.getHeight());
    hdrTarget.init(vkCtx, swapchain.extent());
    brightImage.init(vkCtx, swapchain.extent());
    sceneRenderPass.init(vkCtx, swapchain, hdrTarget);
    swapRenderPass.init(vkCtx, swapchain);

    // Audio — brought up before ResourceManager so the sounds section can
    // create ma_sound objects right away.
    engine::AudioDevice audio;
    audio.init();
    {
        const engine::AudioConfig audioCfg =
            engine::loadAudioConfig("assets/data/audio_config.json");
        // Create every declared group, then apply its volume.
        for (const auto& [id, vol] : audioCfg.groupVolumes) {
            audio.createGroup(id);
            audio.setGroupVolume(id, vol);
        }
    }

    engine::ResourceManager resources;
    resources.init(vkCtx, audio, eventBus);
    // Engine reads the baked manifest produced by tools/manifest_baker
    // (run automatically by CMake before each build). Source files —
    // assets/assets.json + assets/data/factions/*/assets.json — are
    // edited by humans and tools, never read directly by the engine.
    resources.loadManifest(vkCtx, "assets/assets.baked.json");

    engine::DescriptorAllocator descriptors;
    descriptors.init(vkCtx);

    engine::World world;
    world.init(eventBus, inputMap);

    engine::GridRenderer gridRenderer;
    gridRenderer.init(vkCtx, sceneRenderPass, resources, descriptors, world.grid(),
                      engine::SpriteID("tile"), eventBus);

    engine::SpriteRenderer spriteRenderer;
    spriteRenderer.init(vkCtx, sceneRenderPass, resources, descriptors, eventBus);

    engine::TextRenderer textRenderer;
    textRenderer.init(vkCtx, sceneRenderPass, resources, descriptors,
                      engine::FontID("ui_24"), eventBus);

    engine::DebugDraw debugDraw;
    debugDraw.init(vkCtx, sceneRenderPass, resources, descriptors, eventBus);

    engine::PostProcess postProcess;
    postProcess.init(vkCtx, swapRenderPass, resources, descriptors, hdrTarget, eventBus);

    engine::BrightExtractPass brightExtract;
    brightExtract.init(vkCtx, resources, descriptors, hdrTarget, brightImage, eventBus);

    // ── Hot-reload: watch every shader source tracked by the ShaderCache.
    // When a .vert/.frag changes on disk, re-run glslc and republish the
    // renderer-side reload handlers (which rebuild their pipelines).
    engine::FileWatcher shaderWatcher(250);
    resources.shaders().forEachSource(
        [&](engine::ShaderID id, const std::string& path) {
            shaderWatcher.watch(path,
                [&, id](const std::string&) {
                    // Const-cast is safe: ResourceManager owns ShaderCache and
                    // only exposes a const accessor to discourage casual writes.
                    // Reload is an explicit operation in a controlled context.
                    const_cast<engine::ShaderCache&>(resources.shaders())
                        .reload(vkCtx, id);
                });
        });

    engine::FrameRenderer frameRenderer;
    frameRenderer.init(vkCtx, eventBus);

    // GPU-side profiler. Runs independently of the CPU one and surfaces
    // its own ImGui window. Silently no-ops on devices that don't support
    // timestamp queries on the graphics queue.
    engine::GpuProfiler::init(vkCtx);

    engine::ImGuiLayer imgui;
    imgui.init(vkCtx, window, swapchain, swapRenderPass);

    // Discover prefabs (entity templates) before loading the scene.
    // Root at 'assets/data' so prefab ids carry the category prefix
    // (e.g. 'units/player/warrior'); future categories like 'items/...'
    // plug in under the same root without special-casing.
    world.prefabs().loadFromDirectory("assets/data/factions");

    // Load the scene definition.
    const float startAspect =
        static_cast<float>(swapchain.extent().width) /
        static_cast<float>(swapchain.extent().height);
    engine::JsonSceneLoader::load(world, eventBus, world.prefabs(),
        "assets/data/scenes/main.json", startAspect);

    // Sprite material sets are now resolved lazily on first submit — no
    // registration step required here.

    // Hover/select a tile with the mouse. Needs the grid from the loaded
    // scene so we can clamp coords, and DebugDraw for the highlights.
    engine::SelectionSystem selectionSystem;
    selectionSystem.init(world.registry(), eventBus, swapchain,
                         world.grid(), debugDraw);

    // Two queues: scene-pass draws everything into HDR, swap-pass draws
    // post-process + ImGui into the swapchain. Routing is hard-coded —
    // backends know which queue they belong to via how main.cpp wires them.
    engine::RenderQueue sceneQueue;
    engine::RenderQueue swapQueue;

    // ── Subscriptions ─────────────────────────────────────────────
    static const engine::ActionID kTurnEnd("turn.end");
    auto endTurnSub = eventBus.subscribe<engine::ActionTriggeredEvent>(
        [&world](const engine::ActionTriggeredEvent& e) {
            if (e.action == kTurnEnd) world.turnManager().endTurn();
        });

    // Play a short SFX when the player ends a turn. Sound id matches the
    // file path under assets/audio/ without extension.
    auto endTurnSfxSub = eventBus.subscribe<engine::ActionTriggeredEvent>(
        [&audio, &resources](const engine::ActionTriggeredEvent& e) {
            if (e.action != kTurnEnd) return;
            if (engine::Sound* s = resources.getSound(engine::SoundID("player/warrior_spawn"))) {
                audio.play(*s);
            }
        });

    // Resize handler shared between OS and Vulkan-detected triggers. Order
    // matters: HDR target is recreated first, scene render pass's framebuffer
    // binds its view, swap pass's framebuffers reference swapchain images.
    auto handleResize = [&](uint32_t w, uint32_t h) {
        vkDeviceWaitIdle(vkCtx.device());
        swapchain.recreate(vkCtx, w, h);
        hdrTarget.recreate(vkCtx, swapchain.extent());
        brightImage.recreate(vkCtx, swapchain.extent());
        sceneRenderPass.recreateFramebuffers(vkCtx, swapchain, hdrTarget);
        swapRenderPass.recreateFramebuffers(vkCtx, swapchain);
        // Sampler descriptors that point at recreated views need refresh.
        if (swapchain.canPresent()) {
            postProcess.onHdrRecreated(vkCtx, hdrTarget);
            brightExtract.onAttachmentsRecreated(vkCtx, hdrTarget, brightImage);
        }
    };

    auto resizeSub = eventBus.subscribe<engine::WindowResizedEvent>(
        [&](const engine::WindowResizedEvent& e) { handleResize(e.width, e.height); });

    auto outOfDateSub = eventBus.subscribe<engine::SwapchainOutOfDateEvent>(
        [&](const engine::SwapchainOutOfDateEvent&) {
            handleResize(window.getWidth(), window.getHeight());
        });

    // Keep the active camera's aspect ratio in sync with the window.
    auto cameraAspectSub = eventBus.subscribe<engine::WindowResizedEvent>(
        [&world](const engine::WindowResizedEvent& e) {
            if (e.width == 0 || e.height == 0) return;
            const float aspect = static_cast<float>(e.width) / static_cast<float>(e.height);
            auto view = world.registry().view<engine::ActiveCameraTag, engine::CameraComponent2D>();
            for (auto entity : view) {
                view.get<engine::CameraComponent2D>(entity).aspectRatio = aspect;
            }
        });

    // ── Main loop ─────────────────────────────────────────────────
    std::array<float, 4> clearColor{ 0.1f, 0.1f, 0.18f, 1.0f };

    engine::Time::init();

    engine::GameLoopCallbacks callbacks;

    callbacks.onPumpEvents = [&]() {
        engine::Input::beginFrame();

        shaderWatcher.poll();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            imgui.processEvent(event);
            engine::Input::processEvent(event, eventBus);
            if (event.type == SDL_QUIT) {
                eventBus.publish(engine::AppQuitRequestedEvent{});
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                const uint32_t w = static_cast<uint32_t>(event.window.data1);
                const uint32_t h = static_cast<uint32_t>(event.window.data2);
                window.notifyResized(w, h);
                eventBus.publish(engine::WindowResizedEvent{ w, h });
            }
        }
    };

    callbacks.onFixedUpdate = [&](float fixedDt) {
        world.fixedUpdate(fixedDt);
    };

    callbacks.onUpdate = [&](float realDt) {
        imgui.newFrame();

        const auto& turn = world.turnManager();
        ImGui::Begin("Engine Debug");
        ImGui::Text("FPS: %.1f  dt: %.2fms", FP_FPS, realDt * 1000.0f);
        ImGui::Separator();
        ImGui::ColorEdit3("Clear color", clearColor.data());
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Post-process")) {
            ImGui::SliderFloat("Exposure",           &postProcess.settings.exposure,          0.1f, 4.0f);
            ImGui::SliderFloat("Vignette radius",    &postProcess.settings.vignetteRadius,    0.0f, 1.0f);
            ImGui::SliderFloat("Vignette softness",  &postProcess.settings.vignetteSoftness,  0.0f, 1.0f);
            ImGui::SliderFloat("Vignette intensity", &postProcess.settings.vignetteIntensity, 0.0f, 1.0f);
        }
        ImGui::Separator();
        ImGui::Text("Round: %u", turn.round());
        const char* factionName = (turn.currentFaction() == engine::Faction::Player) ? "Player" : "Enemy";
        ImGui::Text("Active faction: %s", factionName);
        ImGui::Text("Actions left: %u", turn.actionsLeft());
        ImGui::Text("[Enter] End turn");

        // ── Screen→World demo ────────────────────────────────────────
        // Picks the active camera out of the registry and shows what
        // world position the mouse cursor is currently over.
        ImGui::Separator();
        const int32_t mx = engine::Input::getMouseX();
        const int32_t my = engine::Input::getMouseY();
        ImGui::Text("Mouse: (%d, %d)", mx, my);

        auto camView = world.registry().view<engine::ActiveCameraTag,
                                             engine::TransformComponent,
                                             engine::CameraComponent2D>();
        for (auto e : camView) {
            const auto& tc = camView.get<engine::TransformComponent>(e);
            const auto& cc = camView.get<engine::CameraComponent2D>(e);
            const glm::vec2 world_xy = engine::screenToWorld(
                { static_cast<float>(mx), static_cast<float>(my) },
                tc, cc, swapchain.extent().width, swapchain.extent().height);
            ImGui::Text("World: (%.2f, %.2f)", world_xy.x, world_xy.y);
            // Grid cell = floor of world coords, since our cells span
            // [col..col+1] × [row..row+1].
            const int32_t col = static_cast<int32_t>(std::floor(world_xy.x));
            const int32_t row = static_cast<int32_t>(std::floor(world_xy.y));
            ImGui::Text("Tile:  (%d, %d)", col, row);
            break;
        }

        ImGui::End();

        world.update(realDt);
        engine::AnimationSystem::update(world.registry(), resources, realDt);
        selectionSystem.update(world.registry(), realDt);

        engine::ProfilerWindow::draw();
        engine::GpuProfilerWindow::draw();

        imgui.render();
    };

    callbacks.onRender = [&](float /*alpha*/) {
        // Phase 0 — wait on this frame slot's fence and acquire a swapchain
        // image. This is the synchronisation point that makes it safe for
        // the rendering backends below to overwrite per-slot resources
        // (UBOs etc.) — the previous use of this slot by the GPU is done.
        if (!frameRenderer.beginFrame(vkCtx, swapchain)) {
            // Swapchain out-of-date / not presentable. Drop DebugDraw's
            // accumulated primitives so the next frame starts clean.
            debugDraw.reset();
            return;
        }

        // Phase 1 — backends submit typed commands to their queues.
        sceneQueue.reset();
        swapQueue.reset();

        // Scene queue (HDR MSAA pass): world geometry + in-world text.
        gridRenderer.submit(sceneQueue, swapchain, world.grid(), world.registry());
        spriteRenderer.submit(sceneQueue, vkCtx, descriptors, swapchain,
                              resources, world.registry());
        debugDraw.submit(sceneQueue, swapchain, world.registry());

        textRenderer.beginFrame(swapchain);
        textRenderer.drawText(sceneQueue, "Hello, fast-photon!",
                              20.f, 40.f,
                              { 1.f, 1.f, 1.f, 1.f });

        // Swap queue (1× swapchain pass): post-process, then ImGui on top.
        postProcess.submit(swapQueue, swapchain);
        imgui.submit(swapQueue);

        // Phase 2 — FrameRenderer records all three phases and submits.
        // The compute phase sits between the scene and swap render passes
        // and runs outside any render pass; FrameRenderer does the layout
        // transitions on brightImage around our recordCompute callback.
        frameRenderer.render(vkCtx, swapchain, sceneRenderPass, swapRenderPass,
            brightImage, clearColor,
            [&](VkCommandBuffer cmd) { sceneQueue.flush(cmd); },
            [&](VkCommandBuffer cmd) { brightExtract.record(cmd); },
            [&](VkCommandBuffer cmd) { swapQueue.flush(cmd); });

        // End-of-frame reset — DebugDraw accumulates across a frame, so
        // drop its primitives only after the queue has consumed them.
        debugDraw.reset();
    };

    engine::GameLoop loop;
    loop.run(appController, callbacks);

    // ── Cleanup (reverse init order) ──────────────────────────────
    vkDeviceWaitIdle(vkCtx.device());

    // Release subscriptions before the bus goes out of scope.
    endTurnSub.release();
    endTurnSfxSub.release();
    resizeSub.release();
    outOfDateSub.release();
    cameraAspectSub.release();

    imgui.shutdown(vkCtx);
    engine::GpuProfiler::shutdown(vkCtx);
    frameRenderer.shutdown(vkCtx);
    selectionSystem.shutdown(world.registry());
    world.shutdown();
    appController.shutdown();
    inputMap.shutdown();

    brightExtract.shutdown(vkCtx);
    postProcess.shutdown(vkCtx);
    debugDraw.shutdown(vkCtx);
    textRenderer.shutdown(vkCtx);
    spriteRenderer.shutdown(vkCtx);
    gridRenderer.shutdown(vkCtx);
    descriptors.shutdown(vkCtx);
    resources.shutdown(vkCtx);
    audio.shutdown();
    swapRenderPass.shutdown(vkCtx);
    sceneRenderPass.shutdown(vkCtx);
    brightImage.shutdown(vkCtx);
    hdrTarget.shutdown(vkCtx);
    swapchain.shutdown(vkCtx);
    vkCtx.shutdown();

    FP_CORE_INFO("Engine shutdown cleanly");
    engine::Log::shutdown();
    return 0;
}
