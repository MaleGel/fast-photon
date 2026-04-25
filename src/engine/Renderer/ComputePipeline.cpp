#include "ComputePipeline.h"
#include "VulkanContext.h"
#include "ResourceManager.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <stdexcept>

namespace engine {

void ComputePipeline::init(const VulkanContext& ctx, const ResourceManager& resources,
                           const ComputePipelineConfig& config) {
    VkShaderModule mod = resources.getShader(config.computeShader);
    FP_CORE_ASSERT(mod != VK_NULL_HANDLE,
                   "Compute shader '{}' not loaded", config.computeShader.c_str());

    // ── Shader stage ─────────────────────────────────────────────
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = mod;
    stage.pName  = "main";

    // ── Layout (push constants + descriptor sets) ────────────────
    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkPushConstantRange pcRange{};
    if (config.pushConstantSize > 0) {
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset     = 0;
        pcRange.size       = config.pushConstantSize;
        layoutCI.pushConstantRangeCount = 1;
        layoutCI.pPushConstantRanges    = &pcRange;
    }

    if (!config.descriptorLayouts.empty()) {
        layoutCI.setLayoutCount = static_cast<uint32_t>(config.descriptorLayouts.size());
        layoutCI.pSetLayouts    = config.descriptorLayouts.data();
    }

    if (vkCreatePipelineLayout(ctx.device(), &layoutCI, nullptr, &m_layout) != VK_SUCCESS)
        throw std::runtime_error("vkCreatePipelineLayout (compute) failed");

    // ── Compute pipeline ─────────────────────────────────────────
    VkComputePipelineCreateInfo pipelineCI{};
    pipelineCI.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCI.stage  = stage;
    pipelineCI.layout = m_layout;

    if (vkCreateComputePipelines(ctx.device(), VK_NULL_HANDLE, 1, &pipelineCI,
                                 nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("vkCreateComputePipelines failed");

    FP_CORE_INFO("ComputePipeline created (shader='{}')", config.computeShader.c_str());
}

void ComputePipeline::shutdown(const VulkanContext& ctx) {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.device(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx.device(), m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
    FP_CORE_TRACE("ComputePipeline destroyed");
}

} // namespace engine
