#pragma once
#include "ResourceTypes.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace engine {

class VulkanContext;
class ResourceManager;

// Describes a single vertex input attribute. 'offset' is bytes from the
// start of the vertex. The pipeline resolves 'binding' to 0 automatically.
struct VertexAttribute {
    uint32_t location;
    VkFormat format;
    uint32_t offset;
};

struct PipelineConfig {
    ShaderID                           vertShader;
    ShaderID                           fragShader;
    uint32_t                           pushConstantSize = 0;   // bytes, 0 = none
    // One entry per descriptor 'set' index, in order. Empty = no sets.
    std::vector<VkDescriptorSetLayout> descriptorLayouts;

    // Primitive topology. Default = triangle list (quads, meshes, sprites).
    // Line list is for debug-draw wireframes.
    VkPrimitiveTopology                topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Optional override of the vertex layout. When empty, the pipeline uses
    // our historical hard-coded layout (vec2 pos + vec2 uv, stride 16).
    // When populated, 'vertexStride' + 'vertexAttributes' describe the buffer.
    uint32_t                           vertexStride     = 0;
    std::vector<VertexAttribute>       vertexAttributes;

    // True for pipelines that synthesise vertices from gl_VertexIndex (e.g.
    // fullscreen triangles) — no vertex buffer will be bound, so declare
    // the input state empty. Disables the default layout fallback.
    bool                               noVertexInput    = false;

    // Depth-buffer interaction. Default = opaque geometry (test + write).
    // For transparent draws set depthWrite=false so back-to-front blending
    // isn't broken by already-written z values from the same layer.
    bool                               depthTest        = true;
    bool                               depthWrite       = true;
};

class Pipeline {
public:
    // Pipelines are bound to a specific VkRenderPass at creation time —
    // we take the raw handle so callers can use any render-pass class.
    void init(const VulkanContext& ctx, VkRenderPass renderPass,
              const ResourceManager& resources, const PipelineConfig& config);
    void shutdown(const VulkanContext& ctx);

    VkPipeline       handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout;   }

private:
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
};

} // namespace engine
