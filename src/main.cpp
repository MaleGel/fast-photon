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
#include "Platform/Window/Window.h"
#include "Platform/Input/Input.h"
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
#include "ECS/Systems/RenderSystem.h"
#include "Gameplay/GridMap/GridMap.h"
#include "Gameplay/TurnManager/TurnManager.h"

// ── ECS setup ────────────────────────────────────────────────────
static void setupECS(entt::registry& registry, engine::GridMap& map) {
    auto camera = registry.create();
    registry.emplace<engine::TagComponent>(camera, "Camera");
    registry.emplace<engine::TransformComponent>(camera, glm::vec3(0, 0, -5));
    registry.emplace<engine::RenderComponent>(camera, glm::vec4(1, 1, 1, 1));

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
    setupECS(registry, map);

    // TurnManager
    engine::TurnManager turnManager;
    turnManager.init({ engine::Faction::Player, engine::Faction::Enemy });

    // Systems
    engine::RenderSystem renderSystem;
    renderSystem.init(registry);

    // ── Main loop ─────────────────────────────────────────────────
    float     clearColor[3] = { 0.1f, 0.1f, 0.18f };
    bool      running       = true;
    SDL_Event event;

    engine::Time::init();
    FP_CORE_INFO("Entering main loop");

    while (running) {
        engine::Input::beginFrame();
        engine::Time::tick();

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            engine::Input::processEvent(event);
            if (event.type == SDL_QUIT) running = false;
        }

        if (FP_KEY_PRESSED(SDLK_ESCAPE)) running = false;
        if (FP_KEY_PRESSED(SDLK_RETURN)) turnManager.endTurn();

        // ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Engine Debug");
        ImGui::Text("FPS: %.1f  dt: %.2fms", FP_FPS, FP_DELTA_TIME * 1000.0f);
        ImGui::Separator();
        ImGui::ColorEdit3("Clear color", clearColor);
        ImGui::Separator();
        ImGui::Text("Round: %u", turnManager.round());
        const char* factionName = (turnManager.currentFaction() == engine::Faction::Player) ? "Player" : "Enemy";
        ImGui::Text("Active faction: %s", factionName);
        ImGui::Text("Actions left: %u", turnManager.actionsLeft());
        ImGui::Text("[Enter] End turn");
        ImGui::End();

        renderSystem.update(registry, FP_DELTA_TIME);

        ImGui::Render();

        // Vulkan frame
        vkWaitForFences(vkCtx.device(), 1, &inFlight, VK_TRUE, UINT64_MAX);
        vkResetFences(vkCtx.device(), 1, &inFlight);

        uint32_t imageIndex = 0;
        vkAcquireNextImageKHR(vkCtx.device(), swapchain.handle(), UINT64_MAX,
                              imageAvailable, VK_NULL_HANDLE, &imageIndex);

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

        gridRenderer.draw(cmdBuffer, swapchain, map);

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
        vkQueuePresentKHR(vkCtx.graphicsQueue(), &present);
    }

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
