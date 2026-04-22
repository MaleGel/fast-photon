#pragma once
#include "ResourceTypes.h"
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>

namespace engine {

class VulkanContext;

// Owns all compiled SPIR-V shader modules loaded during asset registration.
class ShaderCache {
public:
    void shutdown(const VulkanContext& ctx);

    // Load a shader file from disk and register it under 'id'.
    bool load(const VulkanContext& ctx, ShaderID id, const std::string& path);

    // Returns VK_NULL_HANDLE if not found.
    VkShaderModule get(ShaderID id) const;

private:
    std::unordered_map<ShaderID, VkShaderModule> m_shaders;
};

} // namespace engine
