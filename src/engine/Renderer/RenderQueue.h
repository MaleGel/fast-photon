#pragma once
#include "IRenderBackend.h"
#include "RenderLayer.h"

#include <cstring>
#include <type_traits>
#include <vector>

namespace engine {

// Central sorted rendering queue. Every renderer submits typed commands via
// submit<Cmd>(...). At end of frame, flush() stable-sorts them by
// (layer, orderInLayer, z, submissionIx) and dispatches runs of
// consecutive same-backend entries through IRenderBackend::executeBatch.
class RenderQueue {
public:
    // Reset between frames — old commands discarded, submission counter zeroed.
    void reset();

    // Enqueue a draw request. 'Cmd' must be trivially copyable and fit into
    // RenderCommand::kPayloadSize. The payload is memcpy'd — no virtual call,
    // no allocation.
    template<typename Cmd>
    void submit(IRenderBackend* backend,
                RenderLayer layer, int16_t orderInLayer, float z,
                const Cmd& cmd) {
        static_assert(std::is_trivially_copyable_v<Cmd>,
                      "RenderQueue command must be trivially copyable");
        static_assert(sizeof(Cmd) <= RenderCommand::kPayloadSize,
                      "RenderQueue command exceeds payload size");

        RenderCommand entry;
        entry.backend      = backend;
        entry.layer        = layer;
        entry.orderInLayer = orderInLayer;
        entry.z            = z;
        entry.submissionIx = m_nextSubmission++;
        std::memcpy(entry.payload, &cmd, sizeof(Cmd));
        m_entries.push_back(entry);
    }

    // Dispatch the pending commands. Must be called inside an active render pass
    // (inside FrameRenderer's record callback).
    void flush(VkCommandBuffer cmd);

    size_t size() const { return m_entries.size(); }

private:
    std::vector<RenderCommand> m_entries;
    uint32_t                   m_nextSubmission = 0;
};

} // namespace engine
