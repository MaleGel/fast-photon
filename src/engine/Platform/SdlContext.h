#pragma once

namespace engine {

// RAII wrapper around SDL_Init / SDL_Quit. Initialises the video subsystem;
// additional subsystems can be added via initSubsystem() as they are needed.
class SdlContext {
public:
    SdlContext();
    ~SdlContext();

    SdlContext(const SdlContext&)            = delete;
    SdlContext& operator=(const SdlContext&) = delete;

    // Init an extra SDL subsystem (audio, gamecontroller, ...). Throws on failure.
    void initSubsystem(unsigned int flags);

private:
    unsigned int m_initialized = 0;  // bitmask of subsystems we own
};

} // namespace engine
