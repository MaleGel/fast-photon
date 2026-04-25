#pragma once

namespace engine {

// ImGui widget that renders GpuProfiler data. Call draw() once per frame
// between ImGui::NewFrame and ImGui::Render — it opens its own "GPU Profiler"
// window. Mirrors ProfilerWindow for the CPU side.
namespace GpuProfilerWindow {
    void draw();
}

} // namespace engine
