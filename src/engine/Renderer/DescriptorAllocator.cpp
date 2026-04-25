#include "DescriptorAllocator.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <stdexcept>

namespace engine {

// Generous but bounded limits — enough for the foreseeable subsystem needs.
// If hit, raise these numbers in one place.
static constexpr uint32_t kMaxSets              = 256;
static constexpr uint32_t kMaxCombinedSamplers  = 256;
static constexpr uint32_t kMaxUniformBuffers    = 64;
static constexpr uint32_t kMaxStorageBuffers    = 64;

void DescriptorAllocator::init(const VulkanContext& ctx) {
    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxCombinedSamplers },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         kMaxUniformBuffers   },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         kMaxStorageBuffers   },
    };

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags         = 0;  // no individual free — pool is freed as a whole
    ci.maxSets       = kMaxSets;
    ci.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
    ci.pPoolSizes    = sizes;

    if (vkCreateDescriptorPool(ctx.device(), &ci, nullptr, &m_pool) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDescriptorPool failed");

    FP_CORE_INFO("DescriptorAllocator initialized (maxSets={})", kMaxSets);
}

void DescriptorAllocator::shutdown(const VulkanContext& ctx) {
    for (auto layout : m_layouts) {
        vkDestroyDescriptorSetLayout(ctx.device(), layout, nullptr);
    }
    m_layouts.clear();

    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx.device(), m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    FP_CORE_TRACE("DescriptorAllocator destroyed");
}

VkDescriptorSetLayout DescriptorAllocator::createLayout(
    const VulkanContext& ctx,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings) {

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(ctx.device(), &ci, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDescriptorSetLayout failed");

    m_layouts.push_back(layout);
    return layout;
}

VkDescriptorSet DescriptorAllocator::allocate(const VulkanContext& ctx, VkDescriptorSetLayout layout) {
    FP_CORE_ASSERT(m_pool != VK_NULL_HANDLE, "DescriptorAllocator not initialized");

    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(ctx.device(), &ai, &set) != VK_SUCCESS) {
        FP_CORE_ERROR("vkAllocateDescriptorSets failed (pool exhausted?)");
        return VK_NULL_HANDLE;
    }
    return set;
}

} // namespace engine
