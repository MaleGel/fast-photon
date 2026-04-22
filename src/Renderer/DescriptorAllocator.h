#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace engine {

class VulkanContext;

// Thin owner of a VkDescriptorPool + the VkDescriptorSetLayouts created
// through it. A single allocator lives for the whole engine lifetime;
// subsystems ask it for layouts and descriptor sets as needed.
class DescriptorAllocator {
public:
    void init(const VulkanContext& ctx);
    void shutdown(const VulkanContext& ctx);

    // Build a descriptor set layout from a list of bindings.
    // The allocator takes ownership — it will destroy the layout in shutdown().
    VkDescriptorSetLayout createLayout(
        const VulkanContext& ctx,
        const std::vector<VkDescriptorSetLayoutBinding>& bindings);

    // Allocate one empty descriptor set that matches 'layout'.
    // The set is owned by the pool and freed together with it.
    VkDescriptorSet allocate(const VulkanContext& ctx, VkDescriptorSetLayout layout);

    VkDescriptorPool pool() const { return m_pool; }

private:
    VkDescriptorPool                     m_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout>   m_layouts;
};

} // namespace engine
