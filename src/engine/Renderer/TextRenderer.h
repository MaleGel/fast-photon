#pragma once
#include "ResourceTypes.h"
#include "Pipeline.h"
#include "VertexBuffer.h"
#include "UniformBuffer.h"
#include "IRenderBackend.h"
#include "Core/Events/Subscription.h"

#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

namespace engine {

class VulkanContext;
class Swapchain;
class RenderPass;
class ResourceManager;
class DescriptorAllocator;
class EventBus;
class Font;

// Screen-space text. One submit per drawText() call; executeBatch lays out
// every queued string in screen-pixel space, one draw per glyph.
class TextRenderer final : public IRenderBackend {
public:
    void init(const VulkanContext& ctx, const RenderPass& renderPass,
              const ResourceManager& resources, DescriptorAllocator& descriptors,
              FontID fontId, EventBus& bus);
    void shutdown(const VulkanContext& ctx);

    // Must be called once per frame before the first drawText().
    // Refreshes the frame UBO with the current swapchain's screen projection
    // and resets the per-frame string storage.
    void beginFrame(const Swapchain& swapchain);

    // Queue a string at pixel (x, y) with the baseline convention
    // (characters like 'g' hang below y). Submits one RenderCommand to queue.
    void drawText(class RenderQueue& queue,
                  std::string_view text,
                  float x, float y,
                  const glm::vec4& color);

    void executeBatch(VkCommandBuffer cmd,
                      const RenderCommand* commands,
                      size_t commandCount) override;

private:
    void rebuildPipeline();

    const Font*           m_font            = nullptr;

    Pipeline              m_pipeline;

    const VulkanContext*   m_ctx         = nullptr;
    const RenderPass*      m_renderPass  = nullptr;
    const ResourceManager* m_resources   = nullptr;
    PipelineConfig         m_pipelineCfg;
    Subscription           m_shaderReloadSub;
    VertexBuffer          m_quadBuffer;
    UniformBuffer         m_frameUbo;

    VkDescriptorSetLayout m_frameLayout     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_materialLayout  = VK_NULL_HANDLE;
    VkDescriptorSet       m_frameSet        = VK_NULL_HANDLE;
    VkDescriptorSet       m_materialSet     = VK_NULL_HANDLE;

    VkExtent2D            m_currentExtent   = { 0, 0 };

    // Per-frame side storage for string payloads. RenderCommand carries just
    // an index — dodges the "payload must be trivially copyable" rule for
    // std::string.
    std::vector<std::string> m_strings;
};

} // namespace engine
