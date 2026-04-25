#pragma once
#include "Renderer/ResourceTypes.h"
#include "Renderer/Pipeline.h"
#include "Renderer/VertexBuffer.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/IRenderBackend.h"
#include "Core/Events/Subscription.h"

#include <vulkan/vulkan.h>
#include <entt/fwd.hpp>

namespace engine {

class VulkanContext;
class Swapchain;
class RenderPass;
class ResourceManager;
class DescriptorAllocator;
class EventBus;
class GridMap;

// Draws the static terrain grid. A whole grid is submitted as a single
// RenderCommand — the backend's executeBatch then iterates every tile.
class GridRenderer final : public IRenderBackend {
public:
    void init(const VulkanContext& ctx, const RenderPass& renderPass,
              const ResourceManager& resources, DescriptorAllocator& descriptors,
              const GridMap& map, SpriteID tileSprite, EventBus& bus);
    void shutdown(const VulkanContext& ctx);

    // Refresh the camera UBO, then enqueue one RenderCommand on the queue.
    // flush() will later call our executeBatch() with that single command.
    void submit(class RenderQueue& queue, const Swapchain& swapchain,
                const GridMap& map, entt::registry& registry);

    // IRenderBackend: draw all tiles of the captured map.
    void executeBatch(VkCommandBuffer cmd,
                      const RenderCommand* commands,
                      size_t commandCount) override;

private:
    void rebuildPipeline();

    Pipeline              m_pipeline;
    VertexBuffer          m_quadBuffer;
    UniformBuffer         m_frameUbo;

    VkDescriptorSetLayout m_frameLayout     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_materialLayout  = VK_NULL_HANDLE;
    VkDescriptorSet       m_frameSet        = VK_NULL_HANDLE;
    VkDescriptorSet       m_materialSet     = VK_NULL_HANDLE;

    float                 m_uvRect[4]{ 0.f, 0.f, 1.f, 1.f };

    // Captured at submit() time so executeBatch() can iterate the current map
    // + viewport. Not owned.
    const GridMap*        m_currentMap      = nullptr;
    VkExtent2D            m_currentExtent   = { 0, 0 };

    // Captured in init() for the hot-reload path.
    const VulkanContext*   m_ctx          = nullptr;
    const RenderPass*      m_renderPass   = nullptr;
    const ResourceManager* m_resources    = nullptr;
    PipelineConfig         m_pipelineCfg;
    engine::Subscription   m_shaderReloadSub;
};

} // namespace engine
