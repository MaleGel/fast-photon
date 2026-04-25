#pragma once
#include "ResourceTypes.h"
#include "Pipeline.h"
#include "UniformBuffer.h"
#include "IRenderBackend.h"
#include "Core/Events/Subscription.h"

#include <entt/fwd.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

namespace engine {

class VulkanContext;
class Swapchain;
class RenderPass;
class ResourceManager;
class DescriptorAllocator;
class RenderQueue;
class EventBus;

// Immediate-mode world-space debug rendering.
//
// Usage (once per frame):
//   debugDraw.box(...);               // from any system, any number of times
//   debugDraw.line(...);
//   debugDraw.submit(queue, ...);     // enqueues a single RenderCommand
//   // ... RenderQueue::flush() invokes our executeBatch(), which drains
//   // the accumulated vertices into the GPU buffer and issues one draw.
//   debugDraw.reset();                // clear for next frame
//
// Primitives are decomposed to a flat line list on CPU so one pipeline
// (LINE_LIST topology) draws everything in one go.
class DebugDraw final : public IRenderBackend {
public:
    // Hard cap — sized to accommodate several hundred boxes/circles per frame.
    // When exceeded, further submissions are dropped with a one-time warning.
    static constexpr size_t kMaxLines = 2048;

    void init(const VulkanContext& ctx, const RenderPass& renderPass,
              const ResourceManager& resources, DescriptorAllocator& descriptors,
              EventBus& bus);
    void shutdown(const VulkanContext& ctx);

    // ── Primitive API — world space, axis-aligned ───────────────────
    void line(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color);

    // Axis-aligned box outline. 'min' is bottom-left, 'max' is top-right
    // (in world coords — +Y goes down in our Vulkan ortho).
    void box(const glm::vec2& min, const glm::vec2& max, const glm::vec4& color);
    void boxCentered(const glm::vec2& center, const glm::vec2& size,
                     const glm::vec4& color);

    // Circle approximated with N line segments (default 32).
    void circle(const glm::vec2& center, float radius,
                const glm::vec4& color, int segments = 32);

    // Push one RenderCommand on the queue. Call once per frame, after all
    // primitives have been submitted and before RenderQueue::flush().
    void submit(RenderQueue& queue, const Swapchain& swapchain,
                entt::registry& registry);

    // Drop all accumulated primitives. Call at the end of the frame, after
    // the queue has been flushed.
    void reset();

    // IRenderBackend: upload vertex data + one line-list draw.
    void executeBatch(VkCommandBuffer cmd,
                      const RenderCommand* commands,
                      size_t commandCount) override;

private:
    struct Vertex {
        glm::vec2 pos;
        glm::vec4 color;
    };

    void pushLine(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color);
    void rebuildPipeline();

    // CPU-side vertex storage — filled by primitive calls during the frame.
    std::vector<Vertex>    m_vertices;
    bool                   m_warnedOverflow = false;

    // Pipeline + dynamic vertex buffer (host-visible, persistent mapped).
    Pipeline               m_pipeline;

    // Captured in init() so the hot-reload handler can reconstruct the pipeline
    // without the caller re-threading these values every frame.
    const VulkanContext*   m_ctx          = nullptr;
    const RenderPass*      m_renderPass   = nullptr;
    const ResourceManager* m_resources    = nullptr;
    PipelineConfig         m_pipelineCfg;
    Subscription           m_shaderReloadSub;
    VkBuffer               m_vertexBuffer     = VK_NULL_HANDLE;
    VmaAllocation          m_vertexAllocation = VK_NULL_HANDLE;
    void*                  m_vertexMapped     = nullptr;

    // Frame UBO (VP) — identical contract to SpriteRenderer's.
    UniformBuffer          m_frameUbo;
    VkDescriptorSetLayout  m_frameLayout      = VK_NULL_HANDLE;
    VkDescriptorSet        m_frameSet         = VK_NULL_HANDLE;

    VkExtent2D             m_currentExtent    = { 0, 0 };
};

} // namespace engine
