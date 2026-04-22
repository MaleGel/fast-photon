#pragma once

namespace engine {

// Platform layer detected that the user wants to close the application
// (window close button, Alt+F4, SIGINT, ...). Application-level code
// decides what to actually do in response.
struct AppQuitRequestedEvent {};

} // namespace engine
