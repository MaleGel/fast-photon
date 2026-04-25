#pragma once

namespace engine {

// ImGui widget that renders the Profiler's captured data.
// Call draw() once per frame between ImGui::NewFrame and ImGui::Render —
// it opens its own "Profiler" window inside.
namespace ProfilerWindow {
    void draw();
}

} // namespace engine
