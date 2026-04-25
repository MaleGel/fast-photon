#include "ImGuiLayer.h"
#include "RenderQueue.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "SwapchainRenderPass.h"
#include "Platform/Window/Window.h"
#include "Core/Log/Log.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Profiler/GpuProfiler.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>
#include <SDL2/SDL.h>

#include <stdexcept>

namespace engine {

// No per-frame payload — whole ImGui frame state lives in its own context.
struct ImGuiBatchCmd { };
static_assert(sizeof(ImGuiBatchCmd) <= RenderCommand::kPayloadSize, "");

void ImGuiLayer::init(VulkanContext& ctx, Window& window,
                      Swapchain& swapchain, SwapchainRenderPass& renderPass) {
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &poolSize;
    if (vkCreateDescriptorPool(ctx.device(), &dpci, nullptr, &m_pool) != VK_SUCCESS)
        throw std::runtime_error("ImGuiLayer: failed to create descriptor pool");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForVulkan(window.getNativeHandle());

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance       = ctx.instance();
    info.PhysicalDevice = ctx.physicalDevice();
    info.Device         = ctx.device();
    info.QueueFamily    = ctx.graphicsFamily();
    info.Queue          = ctx.graphicsQueue();
    info.DescriptorPool = m_pool;
    info.PipelineInfoMain.RenderPass    = renderPass.handle();
    // ImGui runs in the 1× swapchain pass (post-process resolves MSAA
    // earlier), so its internal pipeline uses VK_SAMPLE_COUNT_1_BIT.
    info.PipelineInfoMain.MSAASamples   = VK_SAMPLE_COUNT_1_BIT;
    info.MinImageCount  = 2;
    info.ImageCount     = swapchain.imageCount();
    ImGui_ImplVulkan_Init(&info);

    FP_CORE_INFO("ImGuiLayer initialized");
}

void ImGuiLayer::shutdown(VulkanContext& ctx) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx.device(), m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    FP_CORE_TRACE("ImGuiLayer destroyed");
}

void ImGuiLayer::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void ImGuiLayer::newFrame() {
    FP_PROFILE_SCOPE("ImGui::newFrame");
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render() {
    FP_PROFILE_SCOPE("ImGui::render");
    ImGui::Render();
}

void ImGuiLayer::submit(RenderQueue& queue) {
    queue.submit<ImGuiBatchCmd>(this, RenderLayer::Debug, /*order*/ 0,
                                /*z*/ 0.f, ImGuiBatchCmd{});
}

void ImGuiLayer::executeBatch(VkCommandBuffer cmd,
                              const RenderCommand* /*commands*/,
                              size_t /*commandCount*/) {
    FP_PROFILE_SCOPE("ImGui::executeBatch");
    FP_GPU_SCOPE(cmd, "ImGui");
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

} // namespace engine
