#include "Pipeline.h"
#include "VulkanContext.h"
#include "RenderPass.h"
#include "ResourceManager.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <stdexcept>

namespace engine {

void Pipeline::init(const VulkanContext& ctx, const RenderPass& renderPass,
                    const ResourceManager& resources, const PipelineConfig& config) {
    // ── Shader stages ─────────────────────────────────────────────
    VkShaderModule vertModule = resources.getShader(config.vertShader);
    VkShaderModule fragModule = resources.getShader(config.fragShader);
    FP_CORE_ASSERT(vertModule != VK_NULL_HANDLE, "Vertex shader '{}' not loaded", config.vertShader.c_str());
    FP_CORE_ASSERT(fragModule != VK_NULL_HANDLE, "Fragment shader '{}' not loaded", config.fragShader.c_str());

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    // ── Vertex input — position(vec2) + uv(vec2) ──────────────────
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(float) * 4;  // vec2 pos + vec2 uv
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;   // vec2 position
    attrs[0].offset   = 0;
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;   // vec2 uv
    attrs[1].offset   = sizeof(float) * 2;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    // ── Input assembly — triangle list ────────────────────────────
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // ── Viewport & scissor (dynamic — set per frame) ──────────────
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    // ── Rasterizer ────────────────────────────────────────────────
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_NONE;
    rasterizer.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    // ── Multisampling — off ───────────────────────────────────────
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ── Color blend — alpha blend ─────────────────────────────────
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable         = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttachment;

    // ── Dynamic state — viewport and scissor set per frame ────────
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynamicStates;

    // ── Pipeline layout (push constants + descriptor sets) ────────
    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkPushConstantRange pcRange{};
    if (config.pushConstantSize > 0) {
        // Visible to both vertex (offset/scale) and fragment (color) stages.
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset     = 0;
        pcRange.size       = config.pushConstantSize;
        layoutCI.pushConstantRangeCount = 1;
        layoutCI.pPushConstantRanges    = &pcRange;
    }

    if (config.descriptorLayout != VK_NULL_HANDLE) {
        layoutCI.setLayoutCount = 1;
        layoutCI.pSetLayouts    = &config.descriptorLayout;
    }

    if (vkCreatePipelineLayout(ctx.device(), &layoutCI, nullptr, &m_layout) != VK_SUCCESS)
        throw std::runtime_error("vkCreatePipelineLayout failed");

    // ── Graphics pipeline ─────────────────────────────────────────
    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.stageCount          = 2;
    pipelineCI.pStages             = stages;
    pipelineCI.pVertexInputState   = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState      = &viewportState;
    pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState   = &multisampling;
    pipelineCI.pColorBlendState    = &colorBlend;
    pipelineCI.pDynamicState       = &dynamicState;
    pipelineCI.layout              = m_layout;
    pipelineCI.renderPass          = renderPass.handle();
    pipelineCI.subpass             = 0;

    if (vkCreateGraphicsPipelines(ctx.device(), VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("vkCreateGraphicsPipelines failed");

    FP_CORE_INFO("Pipeline created (vert='{}', frag='{}')",
                 config.vertShader.c_str(), config.fragShader.c_str());
}

void Pipeline::shutdown(const VulkanContext& ctx) {
    vkDestroyPipeline(ctx.device(), m_pipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device(), m_layout, nullptr);
    FP_CORE_TRACE("Pipeline destroyed");
}

} // namespace engine
