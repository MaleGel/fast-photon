#pragma once
#include <cstdint>

namespace engine {

// Platform layer detected that the user wants to close the application
// (window close button, Alt+F4, SIGINT, ...). Application-level code
// decides what to actually do in response.
struct AppQuitRequestedEvent {};

// OS reported that the window's client area changed size. Width and height
// are the new size in pixels. Either may be zero while the window is
// minimized — subscribers should handle that case.
struct WindowResizedEvent {
    uint32_t width;
    uint32_t height;
};

} // namespace engine
