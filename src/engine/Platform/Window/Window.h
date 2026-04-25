#pragma once
#include <string>
#include <cstdint>

struct SDL_Window;

namespace engine {

struct WindowProps {
    std::string  title;
    uint32_t     width;
    uint32_t     height;

    WindowProps(std::string title = "fast-photon",
                uint32_t width   = 1280,
                uint32_t height  = 720)
        : title(std::move(title)), width(width), height(height) {}
};

class Window {
public:
    explicit Window(const WindowProps& props = {});
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    uint32_t    getWidth()  const { return m_width;  }
    uint32_t    getHeight() const { return m_height; }
    const std::string& getTitle() const { return m_title;  }

    // Update cached size after an OS-level resize. Does not resize the window
    // itself — used by the event pump to keep the cached state in sync.
    void notifyResized(uint32_t width, uint32_t height) {
        m_width  = width;
        m_height = height;
    }

    void setTitle(const std::string& title);

    // Return raw SDL_Window* — needed only for Vulkan/ImGui
    SDL_Window* getNativeHandle() const { return m_window; }

    bool shouldClose() const { return m_shouldClose; }
    void close()             { m_shouldClose = true;  }

private:
    SDL_Window* m_window      = nullptr;
    std::string m_title;
    uint32_t    m_width       = 0;
    uint32_t    m_height      = 0;
    bool        m_shouldClose = false;
};

} // namespace engine
