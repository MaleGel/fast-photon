#pragma once

namespace engine {

// Marker for the camera that render systems should use.
// Exactly one entity is expected to carry this tag at a time.
struct ActiveCameraTag {};

} // namespace engine
