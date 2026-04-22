#pragma once
#include "ResourceTypes.h"
#include "Pipeline.h"
#include "VertexBuffer.h"
#include "UniformBuffer.h"
#include <vulkan/vulkan.h>
#include <entt/fwd.hpp>

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

    // Record draw commands. Looks up the ActiveCameraTag entity in 'registry'
    // to build the view-projection matrix. 'alpha' is the interpolation
    // factor inside the current fixed timestep (0..1).
    void draw(VkCommandBuffer cmd, const Swapchain& swapchain,
              const GridMap& map, entt::registry& registry, float alpha) const;

private:
    Pipeline              m_pipeline;
    VertexBuffer          m_quadBuffer;
    UniformBuffer         m_frameUbo;

    VkDescriptorSetLayout m_frameLayout     = VK_NULL_HANDLE;   // set=0 (VP)
    VkDescriptorSetLayout m_materialLayout  = VK_NULL_HANDLE;   // set=1 (texture)
    VkDescriptorSet       m_frameSet        = VK_NULL_HANDLE;
    VkDescriptorSet       m_materialSet     = VK_NULL_HANDLE;

    float                 m_uvRect[4]{ 0.f, 0.f, 1.f, 1.f };
};

} // namespace engine
