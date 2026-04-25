#pragma once
#include "ResourceTypes.h"
#include "Pipeline.h"
#include "VertexBuffer.h"
#include "UniformBuffer.h"
#include "IRenderBackend.h"
#include "Core/Events/Subscription.h"

#include <entt/fwd.hpp>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace engine {

class VulkanContext;
class Swapchain;
class RenderPass;
class ResourceManager;
class DescriptorAllocator;
class EventBus;

// Draws every entity with TransformComponent + SpriteComponent. Each sprite
// becomes one RenderCommand — the queue sorts them by (layer, z, ...) and
// executeBatch draws the batch with pipeline bound once, rebinding the
// material descriptor set only when the texture id changes.
class SpriteRenderer final : public IRenderBackend {
public:
    void init(const VulkanContext& ctx, const RenderPass& renderPass,
              const ResourceManager& resources, DescriptorAllocator& descriptors,
              EventBus& bus);
    void shutdown(const VulkanContext& ctx);

    // Enqueue every visible sprite on the queue. Lazily allocates descriptor
    // sets for textures the first time they appear — no pre-registration.
    void submit(class RenderQueue& queue, const VulkanContext& ctx,
                DescriptorAllocator& descriptors, const Swapchain& swapchain,
                const ResourceManager& resources, entt::registry& registry);

    // IRenderBackend: draw the batch (assumed to be sprites-only).
    void executeBatch(VkCommandBuffer cmd,
                      const RenderCommand* commands,
                      size_t commandCount) override;

private:
    // Resolve the material set for a texture, creating it if needed.
    VkDescriptorSet resolveMaterialSet(const VulkanContext& ctx,
                                       DescriptorAllocator& descriptors,
                                       const ResourceManager& resources,
                                       TextureID textureId);

    void rebuildPipeline();

    Pipeline              m_pipeline;
    VertexBuffer          m_quadBuffer;
    UniformBuffer         m_frameUbo;

    // Captured in init() for the hot-reload path.
    const VulkanContext*   m_ctx         = nullptr;
    const RenderPass*      m_renderPass  = nullptr;
    const ResourceManager* m_resources   = nullptr;
    PipelineConfig         m_pipelineCfg;
    Subscription           m_shaderReloadSub;

    VkDescriptorSetLayout m_frameLayout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_materialLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_frameSet       = VK_NULL_HANDLE;

    std::unordered_map<TextureID, VkDescriptorSet> m_materialSets;

    // Captured at submit time.
    VkExtent2D            m_currentExtent  = { 0, 0 };
};

} // namespace engine
