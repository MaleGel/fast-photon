#pragma once
#include "IRenderBackend.h"
#include <vulkan/vulkan.h>

union SDL_Event;

namespace engine {

class VulkanContext;
class Window;
class Swapchain;
class SwapchainRenderPass;
class RenderQueue;

// Encapsulates Dear ImGui lifecycle + its Vulkan backend.
//
// Per-frame pattern:
//   layer.processEvent(event);   // inside SDL poll loop
//   layer.newFrame();            // before user ImGui::Begin/End calls
//   // ... build ImGui UI ...
//   layer.render();              // finalises draw data
//   layer.submit(queue);         // enqueue on RenderQueue at Debug layer
class ImGuiLayer final : public IRenderBackend {
public:
    void init(VulkanContext& ctx, Window& window,
              Swapchain& swapchain, SwapchainRenderPass& renderPass);
    void shutdown(VulkanContext& ctx);

    void processEvent(const SDL_Event& event);
    void newFrame();
    void render();                    // ImGui::Render() — finalises draw data
    void submit(RenderQueue& queue);  // enqueue a Debug-layer command

    void executeBatch(VkCommandBuffer cmd,
                      const RenderCommand* commands,
                      size_t commandCount) override;

private:
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
};

} // namespace engine
