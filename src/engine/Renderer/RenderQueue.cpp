#include "RenderQueue.h"

#include <algorithm>

namespace engine {

void RenderQueue::reset() {
    m_entries.clear();
    m_nextSubmission = 0;
}

void RenderQueue::flush(VkCommandBuffer cmd) {
    if (m_entries.empty()) return;

    // Stable sort so same-key entries keep submission order — gives
    // frame-to-frame determinism when layers/z clash.
    std::stable_sort(m_entries.begin(), m_entries.end(),
        [](const RenderCommand& a, const RenderCommand& b) {
            if (a.layer        != b.layer)        return a.layer        < b.layer;
            if (a.orderInLayer != b.orderInLayer) return a.orderInLayer < b.orderInLayer;
            if (a.z            != b.z)            return a.z            < b.z;
            return a.submissionIx < b.submissionIx;
        });

    // Group consecutive same-backend runs and dispatch each as a batch.
    size_t i = 0;
    while (i < m_entries.size()) {
        IRenderBackend* currentBackend = m_entries[i].backend;
        size_t j = i + 1;
        while (j < m_entries.size() && m_entries[j].backend == currentBackend) {
            ++j;
        }
        if (currentBackend) {
            currentBackend->executeBatch(cmd, m_entries.data() + i, j - i);
        }
        i = j;
    }
}

} // namespace engine
