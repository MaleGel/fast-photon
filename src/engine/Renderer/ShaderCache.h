#pragma once
#include "ResourceTypes.h"
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

class VulkanContext;
class EventBus;

// Owns all VkShaderModule objects. Shader sources are GLSL (.vert/.frag) —
// the cache invokes glslc at startup (via VULKAN_SDK) and again on reload,
// so there is exactly one compilation path.
class ShaderCache {
public:
    // Resolves glslc from $VULKAN_SDK. Fails fast if the SDK is missing —
    // we cannot load any shader without it.
    void init(EventBus& bus);
    void shutdown(const VulkanContext& ctx);

    // Compile 'path' (GLSL source) to SPIR-V and create a VkShaderModule
    // under 'id'. 'path' is remembered so reload() can re-run glslc later.
    bool load(const VulkanContext& ctx, ShaderID id, const std::string& path);

    // Re-run glslc on the source of 'id'. On success, destroys the old
    // module (deferred until vkDeviceWaitIdle in the event handler), installs
    // the new one, and publishes ShaderReloadedEvent. On failure, keeps the
    // previous module — the frame keeps rendering with the last known good
    // pipeline.
    bool reload(const VulkanContext& ctx, ShaderID id);

    // Returns VK_NULL_HANDLE if not found.
    VkShaderModule get(ShaderID id) const;

    // Iterate every (id, source path) pair — used by hot-reload wiring to
    // register every source with the FileWatcher.
    template<typename Fn>
    void forEachSource(Fn&& fn) const {
        for (const auto& [id, rec] : m_shaders) fn(id, rec.path);
    }

private:
    struct Record {
        VkShaderModule module = VK_NULL_HANDLE;
        std::string    path;   // GLSL source path
    };

    // Returns true on success; 'outSpv' is populated with the SPIR-V bytes.
    bool compile(const std::string& path, std::vector<uint32_t>& outSpv) const;

    std::string                            m_glslc;   // resolved once in init()
    std::unordered_map<ShaderID, Record>   m_shaders;
    EventBus*                              m_bus = nullptr;
};

} // namespace engine
