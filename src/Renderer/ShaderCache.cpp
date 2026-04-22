#include "ShaderCache.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"

#include <fstream>
#include <vector>

namespace engine {

void ShaderCache::shutdown(const VulkanContext& ctx) {
    for (auto& [id, mod] : m_shaders) {
        vkDestroyShaderModule(ctx.device(), mod, nullptr);
        FP_CORE_TRACE("Shader destroyed: '{}'", id.c_str());
    }
    m_shaders.clear();
}

bool ShaderCache::load(const VulkanContext& ctx, ShaderID id, const std::string& path) {
    // ate = opened at end, so tellg() gives the file size directly
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        FP_CORE_ERROR("Cannot open shader file: {}", path);
        return false;
    }

    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx.device(), &ci, nullptr, &mod) != VK_SUCCESS) {
        FP_CORE_ERROR("vkCreateShaderModule failed: {}", path);
        return false;
    }

    m_shaders[id] = mod;
    FP_CORE_INFO("Shader loaded: '{}' <- {}", id.c_str(), path);
    return true;
}

VkShaderModule ShaderCache::get(ShaderID id) const {
    auto it = m_shaders.find(id);
    if (it == m_shaders.end()) {
        FP_CORE_WARN("Shader not found: '{}'", id.c_str());
        return VK_NULL_HANDLE;
    }
    return it->second;
}

} // namespace engine
