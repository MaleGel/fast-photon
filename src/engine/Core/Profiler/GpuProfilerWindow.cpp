#include "GpuProfilerWindow.h"
#include "GpuProfiler.h"

#include <imgui.h>
#include <vector>

namespace engine {
namespace GpuProfilerWindow {

static ImVec4 colorForDuration(double ms) {
    if (ms < 0.5)  return { 0.65f, 0.65f, 0.65f, 1.0f };
    if (ms < 2.0)  return { 0.45f, 0.90f, 0.45f, 1.0f };
    if (ms < 8.0)  return { 0.95f, 0.85f, 0.30f, 1.0f };
    return                 { 1.00f, 0.45f, 0.40f, 1.0f };
}

// Persistent UI state (same policy as the CPU window).
static bool                             s_paused   = false;
static bool                             s_smoothed = true;
static std::vector<GpuProfiler::Sample> s_frozenSamples;

void draw() {
    if (!ImGui::Begin("GPU Profiler")) {
        ImGui::End();
        return;
    }

    if (!GpuProfiler::enabled()) {
        ImGui::TextDisabled("GPU timestamp queries unavailable on this device.");
        ImGui::End();
        return;
    }

    // ── Controls ────────────────────────────────────────────────────────────
    if (ImGui::Checkbox("Smoothed", &s_smoothed)) {
        if (s_paused) {
            s_frozenSamples = s_smoothed ? GpuProfiler::smoothedSamples()
                                         : GpuProfiler::latestSamples();
        }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Pause", &s_paused)) {
        if (s_paused) {
            s_frozenSamples = s_smoothed ? GpuProfiler::smoothedSamples()
                                         : GpuProfiler::latestSamples();
        }
    }

    // Intrinsic latency reminder — the values on screen lag CPU by
    // (kMaxFramesInFlight - 1) frames because timestamp results are only
    // available after the GPU finishes a frame.
    ImGui::SameLine();
    ImGui::TextDisabled("(~1 frame lag)");

    const std::vector<GpuProfiler::Sample>& samples =
        s_paused ? s_frozenSamples
                 : (s_smoothed ? GpuProfiler::smoothedSamples()
                               : GpuProfiler::latestSamples());

    // Roll-up: sum of top-level scopes gives a "GPU time this frame" total
    // that's comparable to the CPU profiler's Frame line. Nested scopes
    // don't add extra time — they're inside their parent already.
    double gpuTotalMs = 0.0;
    for (const auto& s : samples) {
        if (s.depth == 0) gpuTotalMs += s.durationMs;
    }
    ImGui::Text("GPU total: %.2f ms%s", gpuTotalMs, s_paused ? "  [PAUSED]" : "");

    ImGui::Separator();

    if (samples.empty()) {
        ImGui::TextDisabled("No samples yet (waiting for first resolved frame).");
    } else {
        if (ImGui::BeginTable("gpu_scopes", 2,
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Scope",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (const auto& s : samples) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (s.depth > 0) ImGui::Indent(static_cast<float>(s.depth) * 12.0f);
                ImGui::TextColored(colorForDuration(s.durationMs), "%s", s.name);
                if (s.depth > 0) ImGui::Unindent(static_cast<float>(s.depth) * 12.0f);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(colorForDuration(s.durationMs),
                                   "%.3f ms", s.durationMs);
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
}

} // namespace GpuProfilerWindow
} // namespace engine
