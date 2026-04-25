#pragma once
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>

namespace engine {

enum class RenderLayer : uint8_t;

// One entry in the render queue — a record that some backend wants to draw
// something at a specific layer/order/z. The backend reads its own data out
// of the inline 'payload' buffer.
//
// Inline payload avoids per-submit heap allocations. If a backend needs
// larger or non-POD data (e.g. std::string), keep it in a side storage on
// the backend itself and store just an index in the payload.
struct RenderCommand {
    static constexpr size_t kPayloadSize = 64;

    class IRenderBackend* backend      = nullptr;
    RenderLayer           layer        {};
    int16_t               orderInLayer = 0;
    float                 z            = 0.0f;
    uint32_t              submissionIx = 0;   // tie-breaker (stable order)

    alignas(8) std::byte  payload[kPayloadSize]{};
};

// Each renderer that wants to participate in sorted rendering implements this.
// RenderQueue groups consecutive same-backend entries and hands the whole run
// to executeBatch, so the backend can bind its pipeline/state once per batch.
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    // 'commands' is a contiguous view into the queue's sorted command list,
    // all sharing 'this' as backend. Size is guaranteed ≥ 1.
    virtual void executeBatch(VkCommandBuffer cmd,
                              const RenderCommand* commands,
                              size_t commandCount) = 0;
};

} // namespace engine
