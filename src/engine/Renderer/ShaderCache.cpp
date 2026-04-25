#include "ShaderCache.h"
#include "VulkanContext.h"
#include "RendererEvents.h"
#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace engine {

namespace fs = std::filesystem;

// ── init / shutdown ──────────────────────────────────────────────────────────

void ShaderCache::init(EventBus& bus) {
    m_bus = &bus;

    // Resolve glslc once from VULKAN_SDK. We don't fall back to PATH so the
    // failure mode is loud and obvious if the SDK isn't installed.
    const char* sdk = std::getenv("VULKAN_SDK");
    if (!sdk) {
        FP_CORE_ERROR("ShaderCache: VULKAN_SDK is not set — cannot compile shaders at runtime");
        return;
    }

#if defined(_WIN32)
    fs::path candidate = fs::path(sdk) / "Bin" / "glslc.exe";
#else
    fs::path candidate = fs::path(sdk) / "bin" / "glslc";
#endif
    if (!fs::exists(candidate)) {
        FP_CORE_ERROR("ShaderCache: glslc not found at '{}'", candidate.string());
        return;
    }
    m_glslc = candidate.string();
    FP_CORE_INFO("ShaderCache: glslc = '{}'", m_glslc);
}

void ShaderCache::shutdown(const VulkanContext& ctx) {
    for (auto& [id, rec] : m_shaders) {
        if (rec.module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(ctx.device(), rec.module, nullptr);
            FP_CORE_TRACE("Shader destroyed: '{}'", id.c_str());
        }
    }
    m_shaders.clear();
    m_bus = nullptr;
}

// ── Compile: run glslc on a source file, read back SPIR-V ────────────────────

bool ShaderCache::compile(const std::string& path, std::vector<uint32_t>& outSpv) const {
    if (m_glslc.empty()) {
        FP_CORE_ERROR("ShaderCache: compile('{}') — glslc not resolved", path);
        return false;
    }
    if (!fs::exists(path)) {
        FP_CORE_ERROR("ShaderCache: source file missing: '{}'", path);
        return false;
    }

    // Write to a temp .spv next to the source. Simpler and more portable
    // than piping glslc stdout across Win/Linux.
    const fs::path spv = fs::path(path).string() + ".hotreload.spv";

    // Quote paths in case they contain spaces.
    std::ostringstream cmd;
    cmd << '"' << '"' << m_glslc << "\" \"" << path << "\" -o \"" << spv.string() << '"' << '"';
    // Outer quotes are a Windows cmd.exe quirk: system() wraps the whole
    // string in cmd /C "...", so if the exe path is quoted we need the
    // outer pair to keep the inner quotes intact. Harmless on POSIX.

    const int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        FP_CORE_ERROR("ShaderCache: glslc failed (rc={}) for '{}'", rc, path);
        std::error_code ec;
        fs::remove(spv, ec);
        return false;
    }

    std::ifstream file(spv, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        FP_CORE_ERROR("ShaderCache: cannot open compiled SPIR-V '{}'", spv.string());
        return false;
    }
    const size_t size = static_cast<size_t>(file.tellg());
    if (size == 0 || (size % sizeof(uint32_t)) != 0) {
        FP_CORE_ERROR("ShaderCache: bad SPIR-V size ({} bytes) for '{}'", size, path);
        std::error_code ec;
        fs::remove(spv, ec);
        return false;
    }
    outSpv.resize(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(outSpv.data()),
              static_cast<std::streamsize>(size));
    file.close();

    std::error_code ec;
    fs::remove(spv, ec);   // best-effort — leftover won't break anything
    return true;
}

// ── load ─────────────────────────────────────────────────────────────────────

bool ShaderCache::load(const VulkanContext& ctx, ShaderID id, const std::string& path) {
    std::vector<uint32_t> spv;
    if (!compile(path, spv)) return false;

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size() * sizeof(uint32_t);
    ci.pCode    = spv.data();

    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx.device(), &ci, nullptr, &mod) != VK_SUCCESS) {
        FP_CORE_ERROR("vkCreateShaderModule failed: {}", path);
        return false;
    }

    m_shaders[id] = Record{ mod, path };
    FP_CORE_INFO("Shader loaded: '{}' <- {}", id.c_str(), path);
    return true;
}

// ── reload ───────────────────────────────────────────────────────────────────

bool ShaderCache::reload(const VulkanContext& ctx, ShaderID id) {
    auto it = m_shaders.find(id);
    if (it == m_shaders.end()) {
        FP_CORE_WARN("ShaderCache::reload: unknown id '{}'", id.c_str());
        return false;
    }
    Record& rec = it->second;

    std::vector<uint32_t> spv;
    if (!compile(rec.path, spv)) {
        // Keep the previous module — previous pipelines remain valid.
        return false;
    }

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size() * sizeof(uint32_t);
    ci.pCode    = spv.data();

    VkShaderModule freshModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx.device(), &ci, nullptr, &freshModule) != VK_SUCCESS) {
        FP_CORE_ERROR("ShaderCache::reload: vkCreateShaderModule failed for '{}'", id.c_str());
        return false;
    }

    // Wait for any in-flight frames that still reference the old module via
    // a pipeline, then destroy it. Subscribers will rebuild pipelines in
    // response to ShaderReloadedEvent, so the old module is no longer needed.
    vkDeviceWaitIdle(ctx.device());
    if (rec.module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(ctx.device(), rec.module, nullptr);
    }
    rec.module = freshModule;

    FP_CORE_INFO("Shader reloaded: '{}'", id.c_str());
    if (m_bus) m_bus->publish(ShaderReloadedEvent{ id });
    return true;
}

// ── get ──────────────────────────────────────────────────────────────────────

VkShaderModule ShaderCache::get(ShaderID id) const {
    auto it = m_shaders.find(id);
    if (it == m_shaders.end()) {
        FP_CORE_WARN("Shader not found: '{}'", id.c_str());
        return VK_NULL_HANDLE;
    }
    return it->second.module;
}

} // namespace engine
