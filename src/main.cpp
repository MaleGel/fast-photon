#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>
#include <entt/entt.hpp>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/glm.hpp>

#include <vector>
#include <stdexcept>

#include "Core/Log/Log.h"
#include "Core/StringAtom/StringAtom.h"
#include "Core/Time/Time.h"
#include "Core/Events/EventBus.h"
#include "App/AppController.h"
#include "App/GameLoop.h"
#include "Platform/Window/Window.h"
#include "Platform/Window/WindowEvents.h"
#include "Platform/Input/Input.h"
#include "Platform/Input/InputMap.h"
#include "Renderer/VulkanContext.h"
#include "Renderer/Swapchain.h"
#include "Renderer/RenderPass.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/DescriptorAllocator.h"
#include "Renderer/GridRenderer.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/GridComponent.h"
#include "ECS/Components/RenderComponent.h"
#include "ECS/Components/FactionComponent.h"
#include "ECS/Components/Camera/CameraComponent2D.h"
#include "ECS/Components/Camera/ActiveCameraTag.h"
#include "ECS/Components/Camera/CameraConfig.h"
#include "ECS/Systems/RenderSystem.h"
#include "ECS/Systems/CameraController.h"
#include "Gameplay/GridMap/GridMap.h"
#include "Gameplay/TurnManager/TurnManager.h"

// ── ECS setup ────────────────────────────────────────────────────
static void setupECS(entt::registry& registry, engine::GridMap& map,
                     float aspectRatio) {
    // Camera tuning comes from disk; only runtime-known fields (aspect ratio)
    // stay code-side.
    const engine::CameraConfig camCfg =
        engine::loadCameraConfig("assets/data/camera_config.json");

    auto camera = registry.create();
    registry.emplace<engine::TagComponent>(camera, "Camera");
    // Center on the 8x8 map.
    registry.emplace<engine::TransformComponent>(camera, glm::vec3(4.f, 4.f, 0.f));
    registry.emplace<engine::CameraComponent2D>(camera, engine::CameraComponent2D{
        /*zoom*/        camCfg.zoom,
        /*aspectRatio*/ aspectRatio,
        /*nearPlane*/   camCfg.nearPlane,
        /*farPlane*/    camCfg.farPlane,
        /*panSpeed*/    camCfg.panSpeed,
        /*zoomStep*/    camCfg.zoomStep,
        /*zoomMin*/     camCfg.zoomMin,
        /*zoomMax*/     camCfg.zoomMax,
    });
    registry.emplace<engine::ActiveCameraTag>(camera);

    auto warrior = registry.create();
    registry.emplace<engine::TagComponent>(warrior, "Warrior");
    registry.emplace<engine::TransformComponent>(warrior, glm::vec3(0, 0, 0));
    registry.emplace<engine::GridComponent>(warrior, 2, 3);
    registry.emplace<engine::RenderComponent>(warrior, glm::vec4(0.2f, 0.6f, 1.0f, 1.0f));
    registry.emplace<engine::FactionComponent>(warrior, engine::Faction::Player);
    map.setOccupant(2, 3, warrior);

    auto archer = registry.create();
    registry.emplace<engine::TagComponent>(archer, "Archer");
    registry.emplace<engine::TransformComponent>(archer, glm::vec3(2, 0, 3));
    registry.emplace<engine::GridComponent>(archer, 4, 1);
    registry.emplace<engine::RenderComponent>(archer, glm::vec4(0.2f, 1.0f, 0.4f, 1.0f));
    registry.emplace<engine::FactionComponent>(archer, engine::Faction::Enemy);
    map.setOccupant(4, 1, archer);

    FP_CORE_INFO("ECS: {} entities created", registry.storage<entt::entity>().size());
}

// ── ImGui Vulkan setup ────────────────────────────────────────────
static VkDescriptorPool createImGuiPool(VkDevice device) {
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &poolSize;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    return pool;
}

// ── Command buffer helpers ────────────────────────────────────────
static VkCommandPool createCommandPool(VkDevice device, uint32_t family) {
    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = family;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &pci, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool");
    return pool;
}

static VkCommandBuffer allocateCommandBuffer(VkDevice device, VkCommandPool pool) {
    VkCommandBufferAllocateInfo abai{};
    abai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    abai.commandPool        = pool;
    abai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    abai.commandBufferCount = 1;

    VkCommandBuffer buf = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &abai, &buf) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate command buffer");
    return buf;
}

// ─────────────────────────────────────────────────────────────────
// MAIN
// ─────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    engine::Log::init();
    (void)argc; (void)argv;
    FP_CORE_INFO("=== fast-photon starting ===");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        FP_CORE_CRITICAL("SDL_Init failed: {}", SDL_GetError());
        engine::Log::shutdown();
        return 1;
    }

    engine::EventBus      eventBus;
    engine::InputMap      inputMap;
    inputMap.init(eventBus);
    inputMap.loadBindings("assets/data/input_bindings.json");

    engine::AppController appController;
    appController.init(eventBus);

    engine::Window        window({ "fast-photon", 1280, 720 });
    engine::VulkanContext vkCtx;
    engine::Swapchain     swapchain;
    engine::RenderPass    renderPass;

    vkCtx.init(window.getNativeHandle());
    swapchain.init(vkCtx, window.getWidth(), window.getHeight());
    renderPass.init(vkCtx, swapchain);

    // ResourceManager
    engine::ResourceManager resources;
    resources.init(vkCtx);
    resources.loadManifest(vkCtx, "assets/assets.json");

    // DescriptorAllocator (shared across all subsystems)
    engine::DescriptorAllocator descriptors;
    descriptors.init(vkCtx);

    // GridMap
    engine::GridMap map(8, 8);
    map.at(3, 3).type     = engine::TileType::Mountain;
    map.at(3, 3).passable = false;
    map.at(3, 4).type     = engine::TileType::Water;
    map.at(3, 4).passable = false;
    map.at(1, 1).type     = engine::TileType::Forest;
    map.at(1, 1).movementCost = 2;

    // GridRenderer
    engine::GridRenderer gridRenderer;
    gridRenderer.init(vkCtx, renderPass, resources, descriptors, map,
                      engine::SpriteID("tile"));

    // Command pool + buffer
    VkCommandPool   cmdPool   = createCommandPool(vkCtx.device(), vkCtx.graphicsFamily());
    VkCommandBuffer cmdBuffer = allocateCommandBuffer(vkCtx.device(), cmdPool);

    // Sync primitives
    VkSemaphoreCreateInfo semCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fenCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence     inFlight       = VK_NULL_HANDLE;
    vkCreateSemaphore(vkCtx.device(), &semCI, nullptr, &imageAvailable);
    vkCreateSemaphore(vkCtx.device(), &semCI, nullptr, &renderFinished);
    vkCreateFence(vkCtx.device(), &fenCI, nullptr, &inFlight);

    // ImGui
    VkDescriptorPool imguiPool = createImGuiPool(vkCtx.device());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForVulkan(window.getNativeHandle());

    ImGui_ImplVulkan_InitInfo imguiVkInfo{};
    imguiVkInfo.Instance       = vkCtx.instance();
    imguiVkInfo.PhysicalDevice = vkCtx.physicalDevice();
    imguiVkInfo.Device         = vkCtx.device();
    imguiVkInfo.QueueFamily    = vkCtx.graphicsFamily();
    imguiVkInfo.Queue          = vkCtx.graphicsQueue();
    imguiVkInfo.DescriptorPool = imguiPool;
    imguiVkInfo.PipelineInfoMain.RenderPass = renderPass.handle();
    imguiVkInfo.MinImageCount  = 2;
    imguiVkInfo.ImageCount     = swapchain.imageCount();
    ImGui_ImplVulkan_Init(&imguiVkInfo);
    FP_CORE_INFO("ImGui initialized");

    // ECS
    entt::registry registry;
    const float startAspect =
        static_cast<float>(swapchain.extent().width) /
        static_cast<float>(swapchain.extent().height);
    setupECS(registry, map, startAspect);

    // TurnManager
    engine::TurnManager turnManager;
    turnManager.init(eventBus, { engine::Faction::Player, engine::Faction::Enemy });

    // End-turn binding is driven by the 'turn.end' action. The physical key
    // (Return by default) lives in assets/data/input_bindings.json — not here.
    static const engine::ActionID kTurnEnd("turn.end");
    auto endTurnSub = eventBus.subscribe<engine::ActionTriggeredEvent>(
        [&turnManager](const engine::ActionTriggeredEvent& e) {
            if (e.action == kTurnEnd) turnManager.endTurn();
        });

    // React to window resize: rebuild swapchain + framebuffers so that the
    // next frame renders into buffers matching the new client area.
    auto resizeSub = eventBus.subscribe<engine::WindowResizedEvent>(
        [&](const engine::WindowResizedEvent& e) {
            vkDeviceWaitIdle(vkCtx.device());
            swapchain.recreate(vkCtx, e.width, e.height);
            renderPass.recreateFramebuffers(vkCtx, swapchain);
        });

    // Keep the active camera's aspect ratio in sync with the window.
    auto cameraAspectSub = eventBus.subscribe<engine::WindowResizedEvent>(
        [&registry](const engine::WindowResizedEvent& e) {
            if (e.width == 0 || e.height == 0) return;
            const float aspect = static_cast<float>(e.width) /
                                 static_cast<float>(e.height);
            auto view = registry.view<engine::ActiveCameraTag,
                                      engine::CameraComponent2D>();
            for (auto entity : view) {
                view.get<engine::CameraComponent2D>(entity).aspectRatio = aspect;
            }
        });

    // Systems
    engine::RenderSystem renderSystem;
    renderSystem.init(registry);

    engine::CameraController cameraController;
    cameraController.init(registry, inputMap, eventBus);

    // ── Main loop ─────────────────────────────────────────────────
    float     clearColor[3] = { 0.1f, 0.1f, 0.18f };

    engine::Time::init();

    engine::GameLoopCallbacks callbacks;

    callbacks.onPumpEvents = [&]() {
        engine::Input::beginFrame();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
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
        // No simulation systems yet — this is where movement, AI, and
        // physics will hook in once they exist.
        (void)fixedDt;
    };

    callbacks.onUpdate = [&](float realDt) {
        // ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Engine Debug");
        ImGui::Text("FPS: %.1f  dt: %.2fms", FP_FPS, realDt * 1000.0f);
        ImGui::Separator();
        ImGui::ColorEdit3("Clear color", clearColor);
        ImGui::Separator();
        ImGui::Text("Round: %u", turnManager.round());
        const char* factionName = (turnManager.currentFaction() == engine::Faction::Player) ? "Player" : "Enemy";
        ImGui::Text("Active faction: %s", factionName);
        ImGui::Text("Actions left: %u", turnManager.actionsLeft());
        ImGui::Text("[Enter] End turn");
        ImGui::End();

        cameraController.update(registry, realDt);
        renderSystem.update(registry, realDt);

        ImGui::Render();
    };

    callbacks.onRender = [&](float alpha) {
        // Swapchain is suspended (e.g. window minimized) — skip the frame.
        if (!swapchain.canPresent()) return;

        vkWaitForFences(vkCtx.device(), 1, &inFlight, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult acquireRes = vkAcquireNextImageKHR(
            vkCtx.device(), swapchain.handle(), UINT64_MAX,
            imageAvailable, VK_NULL_HANDLE, &imageIndex);

        // OUT_OF_DATE: swapchain no longer matches the surface — rebuild and
        // try again next frame. The fence stays signalled (we skipped reset).
        if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(vkCtx.device());
            swapchain.recreate(vkCtx, window.getWidth(), window.getHeight());
            renderPass.recreateFramebuffers(vkCtx, swapchain);
            return;
        }
        // SUBOPTIMAL is still renderable; we'll rebuild after present.

        vkResetFences(vkCtx.device(), 1, &inFlight);

        vkResetCommandBuffer(cmdBuffer, 0);

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        VkClearValue clear{};
        clear.color = {{ clearColor[0], clearColor[1], clearColor[2], 1.0f }};

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass        = renderPass.handle();
        rpBegin.framebuffer       = renderPass.framebuffers()[imageIndex];
        rpBegin.renderArea.extent = swapchain.extent();
        rpBegin.clearValueCount   = 1;
        rpBegin.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmdBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        gridRenderer.draw(cmdBuffer, swapchain, map, registry, alpha);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuffer);
        vkCmdEndRenderPass(cmdBuffer);
        vkEndCommandBuffer(cmdBuffer);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &imageAvailable;
        submit.pWaitDstStageMask    = &waitStage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &cmdBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &renderFinished;
        vkQueueSubmit(vkCtx.graphicsQueue(), 1, &submit, inFlight);

        VkSwapchainKHR sc = swapchain.handle();
        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &renderFinished;
        present.swapchainCount     = 1;
        present.pSwapchains        = &sc;
        present.pImageIndices      = &imageIndex;
        VkResult presentRes = vkQueuePresentKHR(vkCtx.graphicsQueue(), &present);

        // Swapchain went out of date between acquire and present, or is
        // SUBOPTIMAL and we want it to match the surface perfectly — rebuild.
        if (presentRes == VK_ERROR_OUT_OF_DATE_KHR ||
            presentRes == VK_SUBOPTIMAL_KHR) {
            vkDeviceWaitIdle(vkCtx.device());
            swapchain.recreate(vkCtx, window.getWidth(), window.getHeight());
            renderPass.recreateFramebuffers(vkCtx, swapchain);
        }
    };

    engine::GameLoop loop;
    loop.run(appController, callbacks);

    // ── Cleanup (reverse init order) ──────────────────────────────
    vkDeviceWaitIdle(vkCtx.device());

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    vkDestroyDescriptorPool(vkCtx.device(), imguiPool, nullptr);
    vkDestroyFence(vkCtx.device(), inFlight, nullptr);
    vkDestroySemaphore(vkCtx.device(), renderFinished, nullptr);
    vkDestroySemaphore(vkCtx.device(), imageAvailable, nullptr);
    vkDestroyCommandPool(vkCtx.device(), cmdPool, nullptr);

    // Release subscriptions before the bus goes out of scope.
    endTurnSub.release();
    resizeSub.release();
    cameraAspectSub.release();
    cameraController.shutdown(registry);
    appController.shutdown();
    inputMap.shutdown();

    gridRenderer.shutdown(vkCtx);
    descriptors.shutdown(vkCtx);
    renderSystem.shutdown(registry);
    resources.shutdown(vkCtx);
    renderPass.shutdown(vkCtx);
    swapchain.shutdown(vkCtx);
    vkCtx.shutdown();

    SDL_Quit();
    FP_CORE_INFO("Engine shutdown cleanly");
    engine::Log::shutdown();
    return 0;
}
