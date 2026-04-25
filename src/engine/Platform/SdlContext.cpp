#include "SdlContext.h"
#include "Core/Log/Log.h"

#include <SDL2/SDL.h>
#include <stdexcept>
#include <string>

namespace engine {

SdlContext::SdlContext() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    m_initialized = SDL_INIT_VIDEO;
    FP_CORE_INFO("SDL initialized (video)");
}

SdlContext::~SdlContext() {
    SDL_Quit();
    FP_CORE_TRACE("SDL shut down");
}

void SdlContext::initSubsystem(unsigned int flags) {
    if (SDL_InitSubSystem(flags) != 0) {
        throw std::runtime_error(std::string("SDL_InitSubSystem failed: ") + SDL_GetError());
    }
    m_initialized |= flags;
}

} // namespace engine
