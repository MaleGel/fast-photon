#pragma once
#include "ResourceTypes.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace engine {

class VulkanContext;
class ResourceManager;

// Configuration mirrors PipelineConfig but for a compute pipeline:
// no render pass, no vertex layout, no blend/depth state — just one shader
// stage and a layout (descriptor sets + push constants).
struct ComputePipelineConfig {
    ShaderID                           computeShader;
    uint32_t                           pushConstantSize = 0;
    std::vector<VkDescriptorSetLayout> descriptorLayouts;
};

// Owns a VkPipeline (compute) + its VkPipelineLayout. The split is identical
// to Pipeline so backends can rebuild pipelines on shader hot-reload the
// same way: shutdown() then init() with the same config.
class ComputePipeline {
public:
    void init(const VulkanContext& ctx, const ResourceManager& resources,
              const ComputePipelineConfig& config);
    void shutdown(const VulkanContext& ctx);

    VkPipeline       handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout;   }

private:
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
};

} // namespace engine
