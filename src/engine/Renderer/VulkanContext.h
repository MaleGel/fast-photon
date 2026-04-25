#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

struct SDL_Window;

namespace engine {

class VulkanContext {
public:
    void init(SDL_Window* window);
    void shutdown();

    VkInstance       instance()       const { return m_instance;       }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice         device()         const { return m_device;         }
    VkQueue          graphicsQueue()  const { return m_graphicsQueue;  }
    VkSurfaceKHR     surface()        const { return m_surface;        }
    uint32_t         graphicsFamily() const { return m_graphicsFamily; }
    VmaAllocator     allocator()      const { return m_allocator;      }

    // Max sample count supported by both color and depth attachments.
    // Capped at 4×: higher (8×/16×) doubles bandwidth for marginal 2D gain.
    VkSampleCountFlagBits msaaSamples() const { return m_msaaSamples;  }

private:
    void createInstance(SDL_Window* window);
    void createSurface(SDL_Window* window);
    void pickPhysicalDevice();
    void createDevice();
    void createAllocator();

    VkInstance       m_instance       = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface        = VK_NULL_HANDLE;
    uint32_t         m_graphicsFamily = 0;
    VmaAllocator     m_allocator      = VK_NULL_HANDLE;

    VkSampleCountFlagBits m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;
};

} // namespace engine
