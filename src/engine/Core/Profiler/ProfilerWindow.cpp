#include "ProfilerWindow.h"
#include "Profiler.h"

#include <imgui.h>
#include <vector>

namespace engine {
namespace ProfilerWindow {

// Pick a row color based on how heavy the scope is.
// <1 ms → subdued gray, <4 ms → green, <16 ms → yellow, else → red.
static ImVec4 colorForDuration(double ms) {
    if (ms < 1.0)  return { 0.65f, 0.65f, 0.65f, 1.0f };
    if (ms < 4.0)  return { 0.45f, 0.90f, 0.45f, 1.0f };
    if (ms < 16.0) return { 0.95f, 0.85f, 0.30f, 1.0f };
    return                 { 1.00f, 0.45f, 0.40f, 1.0f };
}

// Persistent UI state. All static-local to keep the widget self-contained.
// When paused, we freeze a snapshot of whatever view (raw or smoothed) was
// selected at pause time and keep showing it until unpaused.
static bool                      s_paused    = false;
static bool                      s_smoothed  = true;
static std::vector<Profiler::Sample> s_frozenSamples;
static double                    s_frozenFrameMs = 0.0;

void draw() {
    if (!ImGui::Begin("Profiler")) {
        ImGui::End();
        return;
    }

    // ── Controls ────────────────────────────────────────────────────────────
    // Pause + view toggle sit on one row so they're always reachable.
    if (ImGui::Checkbox("Smoothed", &s_smoothed)) {
        // Re-snap the freeze if we're paused — otherwise a view switch while
        // paused would reveal whichever view happened to last be selected.
        if (s_paused) {
            s_frozenSamples = s_smoothed ? Profiler::smoothedSamples()
                                         : Profiler::lastFrameSamples();
            s_frozenFrameMs = s_smoothed ? Profiler::smoothedFrameMs()
                                         : Profiler::lastFrameMs();
        }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Pause", &s_paused)) {
        if (s_paused) {
            s_frozenSamples = s_smoothed ? Profiler::smoothedSamples()
                                         : Profiler::lastFrameSamples();
            s_frozenFrameMs = s_smoothed ? Profiler::smoothedFrameMs()
                                         : Profiler::lastFrameMs();
        }
    }

    // Source for the current draw — the freeze, or live profiler data.
    const std::vector<Profiler::Sample>& samples =
        s_paused ? s_frozenSamples
                 : (s_smoothed ? Profiler::smoothedSamples()
                               : Profiler::lastFrameSamples());
    const double frameMs =
        s_paused ? s_frozenFrameMs
                 : (s_smoothed ? Profiler::smoothedFrameMs()
                               : Profiler::lastFrameMs());

    const double fps = frameMs > 0.0 ? 1000.0 / frameMs : 0.0;
    ImGui::Text("Frame: %.2f ms  (%.0f FPS)%s", frameMs, fps,
                s_paused ? "  [PAUSED]" : "");

    // ── Frame time history plot ─────────────────────────────────────────────
    // The plot always shows the live history — it's the one view where the
    // per-frame jitter is actually the signal we care about, not noise.
    const auto& history = Profiler::frameTimeHistory();
    static float linear[Profiler::kHistorySize];
    for (size_t i = 0; i < Profiler::kHistorySize; ++i) {
        linear[i] = static_cast<float>(history[i]);
    }
    ImGui::PlotLines("##frametime", linear,
                     static_cast<int>(Profiler::kHistorySize),
                     0, nullptr, 0.0f, 33.0f, ImVec2(0, 60));

    ImGui::Separator();

    // ── Scope tree (flat list with depth indent) ────────────────────────────
    if (samples.empty()) {
        ImGui::TextDisabled("No samples captured this frame.");
    } else {
        if (ImGui::BeginTable("scopes", 2,
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

} // namespace ProfilerWindow
} // namespace engine
