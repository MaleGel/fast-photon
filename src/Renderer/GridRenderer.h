#pragma once
#include "ResourceTypes.h"
#include "Pipeline.h"
#include "VertexBuffer.h"
#include <vulkan/vulkan.h>

namespace engine {

class VulkanContext;
class Swapchain;
class RenderPass;
class ResourceManager;
class DescriptorAllocator;
class GridMap;

class GridRenderer {
public:
    void init(const VulkanContext& ctx, const RenderPass& renderPass,
              const ResourceManager& resources, DescriptorAllocator& descriptors,
              const GridMap& map, SpriteID tileSprite);
    void shutdown(const VulkanContext& ctx);

    // Record draw commands into an active command buffer
    void draw(VkCommandBuffer cmd, const Swapchain& swapchain, const GridMap& map) const;

private:
    Pipeline              m_pipeline;
    VertexBuffer          m_quadBuffer;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE; // owned by DescriptorAllocator
    VkDescriptorSet       m_descriptorSet    = VK_NULL_HANDLE; // owned by DescriptorAllocator
    float                 m_uvRect[4]{ 0.f, 0.f, 1.f, 1.f };   // (u0, v0, u1, v1)
};

} // namespace engine
