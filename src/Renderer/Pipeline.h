#pragma once
#include "ResourceTypes.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace engine {

class VulkanContext;
class RenderPass;
class ResourceManager;

struct PipelineConfig {
    ShaderID                           vertShader;
    ShaderID                           fragShader;
    uint32_t                           pushConstantSize = 0;   // bytes, 0 = none
    // One entry per descriptor 'set' index, in order. Empty = no sets.
    std::vector<VkDescriptorSetLayout> descriptorLayouts;
};

class Pipeline {
public:
    void init(const VulkanContext& ctx, const RenderPass& renderPass,
              const ResourceManager& resources, const PipelineConfig& config);
    void shutdown(const VulkanContext& ctx);

    VkPipeline       handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout;   }

private:
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
};

} // namespace engine
