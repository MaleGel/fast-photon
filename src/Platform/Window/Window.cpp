#include "Window.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <SDL2/SDL.h>

namespace engine {

Window::Window(const WindowProps& props)
    : m_title(props.title), m_width(props.width), m_height(props.height)
{
    FP_CORE_ASSERT(SDL_WasInit(SDL_INIT_VIDEO), "SDL video must be initialized before creating a Window");

    m_window = SDL_CreateWindow(
        m_title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int>(m_width), static_cast<int>(m_height),
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    FP_CORE_ASSERT(m_window != nullptr, "SDL_CreateWindow failed: {}", SDL_GetError());
    FP_CORE_INFO("Window created ('{}', {}x{})", m_title, m_width, m_height);
}

Window::~Window() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
        FP_CORE_TRACE("Window destroyed");
    }
}

void Window::setTitle(const std::string& title) {
    m_title = title;
    SDL_SetWindowTitle(m_window, m_title.c_str());
}

} // namespace engine
