#pragma once
#include "ResourceTypes.h"

namespace engine {

// FrameRenderer detected that the current swapchain no longer matches the
// surface (out-of-date or suboptimal). Subscribers should rebuild the
// swapchain and any framebuffers hooked to its image views.
struct SwapchainOutOfDateEvent {};

// A shader source was recompiled at runtime and its VkShaderModule replaced.
// Pipelines referencing this shader must be destroyed and recreated —
// Vulkan pipelines embed VkShaderModule handles at creation time.
struct ShaderReloadedEvent {
    ShaderID id;
};

} // namespace engine
