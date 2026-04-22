#include "VulkanContext.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <SDL2/SDL_vulkan.h>
#include <vector>
#include <stdexcept>

namespace engine {

void VulkanContext::init(SDL_Window* window) {
    createInstance(window);
    createSurface(window);
    pickPhysicalDevice();
    createDevice();
    createAllocator();
    FP_CORE_INFO("VulkanContext initialized");
}

void VulkanContext::shutdown() {
    vmaDestroyAllocator(m_allocator);
    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
    FP_CORE_TRACE("VulkanContext destroyed");
}

// ── Private ───────────────────────────────────────────────────────

void VulkanContext::createInstance(SDL_Window* window) {
    unsigned int extCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr);
    std::vector<const char*> exts(extCount);
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, exts.data());

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "fast-photon";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");

    FP_CORE_INFO("Vulkan instance created");
}

void VulkanContext::createSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, m_instance, &m_surface))
        throw std::runtime_error("SDL_Vulkan_CreateSurface failed");
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    FP_CORE_ASSERT(count > 0, "No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    for (auto pd : devices) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qProps.data());

        for (uint32_t i = 0; i < qCount; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, m_surface, &present);
            if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                m_physicalDevice = pd;
                m_graphicsFamily = i;
                break;
            }
        }
        if (m_physicalDevice) break;
    }

    FP_CORE_ASSERT(m_physicalDevice != VK_NULL_HANDLE, "No suitable GPU found");

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    FP_CORE_INFO("GPU: {}", props.deviceName);
}

void VulkanContext::createDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = m_graphicsFamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = devExts;

    if (vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice failed");

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    FP_CORE_INFO("Vulkan logical device created");
}

void VulkanContext::createAllocator() {
    VmaAllocatorCreateInfo ai{};
    ai.instance         = m_instance;
    ai.physicalDevice   = m_physicalDevice;
    ai.device           = m_device;
    ai.vulkanApiVersion = VK_API_VERSION_1_2;

    if (vmaCreateAllocator(&ai, &m_allocator) != VK_SUCCESS)
        throw std::runtime_error("vmaCreateAllocator failed");

    FP_CORE_INFO("VMA allocator created");
}

} // namespace engine
