#include "DebugDraw.h"
#include "RenderQueue.h"
#include "RendererEvents.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "ResourceManager.h"
#include "DescriptorAllocator.h"

#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/Camera/CameraComponent2D.h"
#include "Scene/Components/Camera/ActiveCameraTag.h"
#include "Scene/Components/Camera/CameraMath.h"

#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Profiler/GpuProfiler.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace engine {

struct FrameUniforms {
    glm::mat4 viewProjection;
};

// Empty payload — entire frame's vertex data lives in our own buffer, so the
// queue command carries no per-instance data.
struct DebugBatchCmd {};
static_assert(sizeof(DebugBatchCmd) <= RenderCommand::kPayloadSize, "");

// ── Init / shutdown ──────────────────────────────────────────────────────────

void DebugDraw::init(const VulkanContext& ctx, const RenderPass& renderPass,
                    const ResourceManager& resources,
                    DescriptorAllocator& descriptors,
                    EventBus& bus) {
    m_ctx        = &ctx;
    m_renderPass = &renderPass;
    m_resources  = &resources;

    // ── Descriptor layout: set=0 for frame UBO (VP matrix) ─────────────
    VkDescriptorSetLayoutBinding frameBinding{};
    frameBinding.binding         = 0;
    frameBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBinding.descriptorCount = 1;
    frameBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    m_frameLayout = descriptors.createLayout(ctx, { frameBinding });

    // ── Pipeline ───────────────────────────────────────────────────────
    m_pipelineCfg = PipelineConfig{};
    m_pipelineCfg.vertShader        = ShaderID("debug_vert");
    m_pipelineCfg.fragShader        = ShaderID("debug_frag");
    m_pipelineCfg.descriptorLayouts = { m_frameLayout };
    m_pipelineCfg.topology          = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    // Vertex layout: vec2 pos + vec4 color.
    m_pipelineCfg.vertexStride = sizeof(Vertex);
    m_pipelineCfg.vertexAttributes = {
        { 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Vertex, pos)   },
        { 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) },
    };

    m_pipeline.init(ctx, renderPass.handle(), resources, m_pipelineCfg);

    // Rebuild on hot-reload of either of our shaders.
    m_shaderReloadSub = bus.subscribe<ShaderReloadedEvent>(
        [this](const ShaderReloadedEvent& e) {
            if (e.id == m_pipelineCfg.vertShader || e.id == m_pipelineCfg.fragShader) {
                rebuildPipeline();
            }
        });

    // ── Descriptor set + frame UBO ─────────────────────────────────────
    m_frameSet = descriptors.allocate(ctx, m_frameLayout);
    m_frameUbo.init(ctx, sizeof(FrameUniforms));

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_frameUbo.handle();
    bufInfo.range  = sizeof(FrameUniforms);

    VkWriteDescriptorSet uboWrite{};
    uboWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uboWrite.dstSet          = m_frameSet;
    uboWrite.dstBinding      = 0;
    uboWrite.descriptorCount = 1;
    uboWrite.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.pBufferInfo     = &bufInfo;
    vkUpdateDescriptorSets(ctx.device(), 1, &uboWrite, 0, nullptr);

    // ── Dynamic vertex buffer ──────────────────────────────────────────
    // Each line = 2 vertices. Allocate room for the hard cap up front.
    const VkDeviceSize bufSize = sizeof(Vertex) * kMaxLines * 2;

    VkBufferCreateInfo bufferCI{};
    bufferCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size        = bufSize;
    bufferCI.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo{};
    if (vmaCreateBuffer(ctx.allocator(), &bufferCI, &allocCI,
                        &m_vertexBuffer, &m_vertexAllocation, &allocInfo) != VK_SUCCESS)
        throw std::runtime_error("DebugDraw: vertex buffer alloc failed");

    m_vertexMapped = allocInfo.pMappedData;
    FP_CORE_ASSERT(m_vertexMapped != nullptr, "DebugDraw: buffer not persistently mapped");

    m_vertices.reserve(kMaxLines * 2);
    FP_CORE_INFO("DebugDraw initialized (cap={} lines)", kMaxLines);
}

void DebugDraw::rebuildPipeline() {
    // vkDeviceWaitIdle was already issued by ShaderCache::reload before this
    // event fired, so the pipeline is safe to tear down synchronously here.
    m_pipeline.shutdown(*m_ctx);
    m_pipeline.init(*m_ctx, m_renderPass->handle(), *m_resources, m_pipelineCfg);
    FP_CORE_INFO("DebugDraw: pipeline rebuilt after shader reload");
}

void DebugDraw::shutdown(const VulkanContext& ctx) {
    m_shaderReloadSub.release();
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.allocator(), m_vertexBuffer, m_vertexAllocation);
        m_vertexBuffer     = VK_NULL_HANDLE;
        m_vertexAllocation = VK_NULL_HANDLE;
        m_vertexMapped     = nullptr;
    }
    m_frameUbo.destroy(ctx);
    m_pipeline.shutdown(ctx);
    m_vertices.clear();
    FP_CORE_TRACE("DebugDraw destroyed");
}

// ── Primitive API ────────────────────────────────────────────────────────────

void DebugDraw::pushLine(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color) {
    if (m_vertices.size() / 2 >= kMaxLines) {
        if (!m_warnedOverflow) {
            FP_CORE_WARN("DebugDraw: exceeded {} line cap — further submissions dropped this frame",
                         kMaxLines);
            m_warnedOverflow = true;
        }
        return;
    }
    m_vertices.push_back({ a, color });
    m_vertices.push_back({ b, color });
}

void DebugDraw::line(const glm::vec2& a, const glm::vec2& b, const glm::vec4& color) {
    pushLine(a, b, color);
}

void DebugDraw::box(const glm::vec2& min, const glm::vec2& max, const glm::vec4& color) {
    // 4 edges of the rectangle.
    const glm::vec2 tl{ min.x, min.y };
    const glm::vec2 tr{ max.x, min.y };
    const glm::vec2 br{ max.x, max.y };
    const glm::vec2 bl{ min.x, max.y };
    pushLine(tl, tr, color);
    pushLine(tr, br, color);
    pushLine(br, bl, color);
    pushLine(bl, tl, color);
}

void DebugDraw::boxCentered(const glm::vec2& center, const glm::vec2& size,
                            const glm::vec4& color) {
    const glm::vec2 half = size * 0.5f;
    box(center - half, center + half, color);
}

void DebugDraw::circle(const glm::vec2& center, float radius,
                       const glm::vec4& color, int segments) {
    if (segments < 3) segments = 3;
    const float step = 6.2831853f / static_cast<float>(segments);
    glm::vec2 prev{ center.x + radius, center.y };
    for (int i = 1; i <= segments; ++i) {
        const float a = step * static_cast<float>(i);
        glm::vec2 curr{ center.x + std::cos(a) * radius,
                        center.y + std::sin(a) * radius };
        pushLine(prev, curr, color);
        prev = curr;
    }
}

// ── Frame lifecycle ──────────────────────────────────────────────────────────

void DebugDraw::submit(RenderQueue& queue, const Swapchain& swapchain,
                      entt::registry& registry) {
    FP_PROFILE_SCOPE("DebugDraw::submit");
    // Resolve active camera → VP, update UBO.
    auto view = registry.view<const ActiveCameraTag,
                              const TransformComponent,
                              const CameraComponent2D>();
    glm::mat4 vp(1.0f);
    bool haveCamera = false;
    for (auto e : view) {
        vp = buildViewProjection(view.get<const TransformComponent>(e),
                                 view.get<const CameraComponent2D>(e));
        haveCamera = true;
        break;
    }
    if (!haveCamera) return;

    FrameUniforms* uni = m_frameUbo.mapped<FrameUniforms>();
    uni->viewProjection = vp;

    m_currentExtent = swapchain.extent();

    // Always enqueue — executeBatch handles the empty-vertices case cheaply
    // so debug draw works across passes even with nothing submitted.
    queue.submit<DebugBatchCmd>(this, RenderLayer::Debug, /*order*/ 0,
                                /*z*/ 0.0f, DebugBatchCmd{});
}

void DebugDraw::reset() {
    m_vertices.clear();
    m_warnedOverflow = false;
}

// ── executeBatch ─────────────────────────────────────────────────────────────

void DebugDraw::executeBatch(VkCommandBuffer cmd,
                             const RenderCommand* /*commands*/,
                             size_t /*commandCount*/) {
    FP_PROFILE_SCOPE("DebugDraw::executeBatch");
    FP_GPU_SCOPE(cmd, "DebugDraw");
    if (m_vertices.empty()) return;

    // Upload accumulated vertices into our persistently-mapped buffer.
    std::memcpy(m_vertexMapped, m_vertices.data(),
                m_vertices.size() * sizeof(Vertex));

    const float w = static_cast<float>(m_currentExtent.width);
    const float h = static_cast<float>(m_currentExtent.height);
    VkViewport viewport{ 0.f, 0.f, w, h, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ { 0, 0 }, m_currentExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline.layout(), 0, 1, &m_frameSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
    vkCmdDraw(cmd, static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);
}

} // namespace engine
