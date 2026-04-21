#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <spdlog/spdlog.h>
#include <entt/entt.hpp>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/glm.hpp>

#include <vector>
#include <stdexcept>
#include <string>

#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

// ─────────────────────────────────────────────
// ECS
// ─────────────────────────────────────────────
struct Transform {
    glm::vec3 position;
    glm::vec3 scale;
};

struct Tag {
    std::string name;
};

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────
#define VK_CHECK(result, msg) \
    if ((result) != VK_SUCCESS) { \
        FP_CORE_ERROR("[Vulkan] {} (code: {})", msg, static_cast<int>(result)); \
        throw std::runtime_error(msg); \
    }

// ─────────────────────────────────────────────
// Vulkan
// ─────────────────────────────────────────────
struct VulkanContext {
    VkInstance               instance       = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice = VK_NULL_HANDLE;
    VkDevice                 device         = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue  = VK_NULL_HANDLE;
    uint32_t                 graphicsFamily = 0;
    VkSurfaceKHR             surface        = VK_NULL_HANDLE;
    VkSwapchainKHR           swapchain      = VK_NULL_HANDLE;
    VkFormat                 swapFormat     = VK_FORMAT_UNDEFINED;
    VkExtent2D               swapExtent     = {};
    std::vector<VkImage>     swapImages;
    std::vector<VkImageView> swapImageViews;
    VkRenderPass             renderPass     = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool            cmdPool        = VK_NULL_HANDLE;
    VkCommandBuffer          cmdBuffer      = VK_NULL_HANDLE;
    VkSemaphore              imageAvailable = VK_NULL_HANDLE;
    VkSemaphore              renderFinished = VK_NULL_HANDLE;
    VkFence                  inFlight       = VK_NULL_HANDLE;
    VkDescriptorPool         imguiPool      = VK_NULL_HANDLE;
};

// ─────────────────────────────────────────────
// Vulkan Init
// ─────────────────────────────────────────────
void initVulkan(VulkanContext& vk, SDL_Window* window, uint32_t w, uint32_t h) {
    {
        unsigned int count = 0;
        SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr);
        std::vector<const char*> exts(count);
        SDL_Vulkan_GetInstanceExtensions(window, &count, exts.data());

        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "EngineTest";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_2;

        VkInstanceCreateInfo ci{};
        ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo        = &appInfo;
        ci.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();

        VK_CHECK(vkCreateInstance(&ci, nullptr, &vk.instance), "vkCreateInstance");
        FP_CORE_INFO("Vulkan instance created");
    }

    if (!SDL_Vulkan_CreateSurface(window, vk.instance, &vk.surface)) throw std::runtime_error("SDL_Vulkan_CreateSurface failed"); {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(vk.instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(vk.instance, &count, devices.data());

        for (auto pd : devices) {
            uint32_t qCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> qProps(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qProps.data());

            for (uint32_t i = 0; i < qCount; ++i) {
                VkBool32 present = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, vk.surface, &present);
                if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    vk.physicalDevice = pd;
                    vk.graphicsFamily = i;
                    break;
                }
            }
            if (vk.physicalDevice) break;
        }
        if (!vk.physicalDevice) throw std::runtime_error("No suitable GPU found");

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(vk.physicalDevice, &props);
        FP_CORE_INFO("GPU: {}", props.deviceName);
    }

    {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{};
        qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = vk.graphicsFamily;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &priority;

        const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkDeviceCreateInfo dci{};
        dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount    = 1;
        dci.pQueueCreateInfos       = &qci;
        dci.enabledExtensionCount   = 1;
        dci.ppEnabledExtensionNames = devExts;

        VK_CHECK(vkCreateDevice(vk.physicalDevice, &dci, nullptr, &vk.device), "vkCreateDevice");
        vkGetDeviceQueue(vk.device, vk.graphicsFamily, 0, &vk.graphicsQueue);
        FP_CORE_INFO("Vulkan logical device created");
    }

    {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physicalDevice, vk.surface, &caps);

        uint32_t fmtCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &fmtCount, formats.data());

        VkSurfaceFormatKHR chosen = formats[0];
        for (auto& f : formats)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                chosen = f;

        vk.swapFormat = chosen.format;
        vk.swapExtent = caps.currentExtent.width == UINT32_MAX
            ? VkExtent2D{ w, h }
            : caps.currentExtent;

        VkSwapchainCreateInfoKHR sci{};
        sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sci.surface          = vk.surface;
        sci.minImageCount    = std::min(caps.minImageCount + 1,
                                caps.maxImageCount > 0 ? caps.maxImageCount : UINT32_MAX);
        sci.imageFormat      = vk.swapFormat;
        sci.imageColorSpace  = chosen.colorSpace;
        sci.imageExtent      = vk.swapExtent;
        sci.imageArrayLayers = 1;
        sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform     = caps.currentTransform;
        sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped          = VK_TRUE;

        VK_CHECK(vkCreateSwapchainKHR(vk.device, &sci, nullptr, &vk.swapchain), "vkCreateSwapchainKHR");

        uint32_t imgCount = 0;
        vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &imgCount, nullptr);
        vk.swapImages.resize(imgCount);
        vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &imgCount, vk.swapImages.data());

        vk.swapImageViews.resize(imgCount);
        for (uint32_t i = 0; i < imgCount; ++i) {
            VkImageViewCreateInfo ivci{};
            ivci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ivci.image                           = vk.swapImages[i];
            ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            ivci.format                          = vk.swapFormat;
            ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            ivci.subresourceRange.levelCount     = 1;
            ivci.subresourceRange.layerCount     = 1;
            VK_CHECK(vkCreateImageView(vk.device, &ivci, nullptr, &vk.swapImageViews[i]), "vkCreateImageView");
        }
        FP_CORE_INFO("Swapchain created ({}x{})", vk.swapExtent.width, vk.swapExtent.height);
    }

    {
        VkAttachmentDescription color{};
        color.format         = vk.swapFormat;
        color.samples        = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpci{};
        rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments    = &color;
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;
        rpci.dependencyCount = 1;
        rpci.pDependencies   = &dep;

        VK_CHECK(vkCreateRenderPass(vk.device, &rpci, nullptr, &vk.renderPass), "vkCreateRenderPass");
    }

    {
        vk.framebuffers.resize(vk.swapImageViews.size());
        for (size_t i = 0; i < vk.swapImageViews.size(); ++i) {
            VkFramebufferCreateInfo fci{};
            fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fci.renderPass      = vk.renderPass;
            fci.attachmentCount = 1;
            fci.pAttachments    = &vk.swapImageViews[i];
            fci.width           = vk.swapExtent.width;
            fci.height          = vk.swapExtent.height;
            fci.layers          = 1;
            VK_CHECK(vkCreateFramebuffer(vk.device, &fci, nullptr, &vk.framebuffers[i]), "vkCreateFramebuffer");
        }
    }

    {
        VkCommandPoolCreateInfo pci{};
        pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = vk.graphicsFamily;
        pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(vk.device, &pci, nullptr, &vk.cmdPool), "vkCreateCommandPool");

        VkCommandBufferAllocateInfo abai{};
        abai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        abai.commandPool        = vk.cmdPool;
        abai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        abai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(vk.device, &abai, &vk.cmdBuffer), "vkAllocateCommandBuffers");
    }

    {
        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo     fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateSemaphore(vk.device, &sci, nullptr, &vk.imageAvailable), "semaphore");
        VK_CHECK(vkCreateSemaphore(vk.device, &sci, nullptr, &vk.renderFinished), "semaphore");
        VK_CHECK(vkCreateFence(vk.device, &fci, nullptr, &vk.inFlight), "fence");
    }

    {
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
        };
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        dpci.maxSets       = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes    = poolSizes;
        VK_CHECK(vkCreateDescriptorPool(vk.device, &dpci, nullptr, &vk.imguiPool), "imguiPool");
    }

    FP_CORE_INFO("Vulkan init complete ✓");
}

// ─────────────────────────────────────────────
// Cleanup
// ─────────────────────────────────────────────
void cleanupVulkan(VulkanContext& vk) {
    vkDeviceWaitIdle(vk.device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    vkDestroyDescriptorPool(vk.device, vk.imguiPool, nullptr);
    vkDestroyFence(vk.device, vk.inFlight, nullptr);
    vkDestroySemaphore(vk.device, vk.renderFinished, nullptr);
    vkDestroySemaphore(vk.device, vk.imageAvailable, nullptr);
    vkDestroyCommandPool(vk.device, vk.cmdPool, nullptr);
    for (auto fb : vk.framebuffers)   vkDestroyFramebuffer(vk.device, fb, nullptr);
    vkDestroyRenderPass(vk.device, vk.renderPass, nullptr);
    for (auto iv : vk.swapImageViews) vkDestroyImageView(vk.device, iv, nullptr);
    vkDestroySwapchainKHR(vk.device, vk.swapchain, nullptr);
    vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);
    vkDestroyDevice(vk.device, nullptr);
    vkDestroyInstance(vk.instance, nullptr);
}

// ─────────────────────────────────────────────
// EnTT
// ─────────────────────────────────────────────
void setupECS(entt::registry& registry) {
    auto camera = registry.create();
    registry.emplace<Tag>(camera, "Camera");
    registry.emplace<Transform>(camera, glm::vec3(0, 0, -5), glm::vec3(1));

    auto cube = registry.create();
    registry.emplace<Tag>(cube, "Cube");
    registry.emplace<Transform>(cube, glm::vec3(0, 0, 0), glm::vec3(1));

    auto light = registry.create();
    registry.emplace<Tag>(light, "PointLight");
    registry.emplace<Transform>(light, glm::vec3(3, 4, 2), glm::vec3(0.1f));

    FP_CORE_INFO("ECS: {} entities created", registry.storage<entt::entity>().size());
}

// ─────────────────────────────────────────────
// MAIN
// ─────────────────────────────────────────────
int main(int argc, char* argv[]) {
    engine::Log::init();

    (void)argc; (void)argv;
    FP_CORE_INFO("=== fast-photon starting ===");

    // SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(SDL_GetError());

    constexpr uint32_t WIDTH  = 1280;
    constexpr uint32_t HEIGHT = 720;

    SDL_Window* window = SDL_CreateWindow(
        "FastPhotone",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );
    if (!window) throw std::runtime_error(SDL_GetError());
    FP_CORE_INFO("Window created ({}x{})", WIDTH, HEIGHT);

    // Vulkan
    VulkanContext vk{};
    initVulkan(vk, window, WIDTH, HEIGHT);

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo imguiVkInfo{};
    imguiVkInfo.Instance        = vk.instance;
    imguiVkInfo.PhysicalDevice  = vk.physicalDevice;
    imguiVkInfo.Device          = vk.device;
    imguiVkInfo.QueueFamily     = vk.graphicsFamily;
    imguiVkInfo.Queue           = vk.graphicsQueue;
    imguiVkInfo.DescriptorPool  = vk.imguiPool;
    imguiVkInfo.PipelineInfoMain.RenderPass = vk.renderPass;
    imguiVkInfo.MinImageCount   = 2;
    imguiVkInfo.ImageCount      = static_cast<uint32_t>(vk.swapImages.size());
    ImGui_ImplVulkan_Init(&imguiVkInfo);
    FP_CORE_INFO("ImGui initialized");

    // ECS
    entt::registry registry;
    setupECS(registry);

    // ── Main loop ───────────────────────────────
    bool running = true;
    SDL_Event event;

    float clearColor[3] = { 0.1f, 0.1f, 0.18f };
    int   frameCount    = 0;
    float fps           = 0.0f;
    uint32_t lastTick   = SDL_GetTicks();

    FP_CORE_INFO("Entering main loop");

    while (running) {
        // Events
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        // FPS
        ++frameCount;
        uint32_t now = SDL_GetTicks();
        if (now - lastTick >= 500) {
            fps       = frameCount * 1000.0f / (now - lastTick);
            frameCount = 0;
            lastTick  = now;
        }

        // ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Engine Debug");
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Separator();
        ImGui::ColorEdit3("Clear color", clearColor);
        ImGui::Separator();
        ImGui::Text("ECS Entities:");

        auto view = registry.view<Tag, Transform>();
        for (auto [entity, tag, transform] : view.each()) {
            ImGui::Text("  [%u] %s  pos=(%.1f, %.1f, %.1f)",
                static_cast<uint32_t>(entity),
                tag.name.c_str(),
                transform.position.x,
                transform.position.y,
                transform.position.z
            );
        }
        ImGui::End();
        ImGui::Render();

        // Vulkan frame
        vkWaitForFences(vk.device, 1, &vk.inFlight, VK_TRUE, UINT64_MAX);
        vkResetFences(vk.device, 1, &vk.inFlight);

        uint32_t imageIndex = 0;
        vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX,
                              vk.imageAvailable, VK_NULL_HANDLE, &imageIndex);

        vkResetCommandBuffer(vk.cmdBuffer, 0);

        VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(vk.cmdBuffer, &begin);

        VkClearValue clear{};
        clear.color = {{ clearColor[0], clearColor[1], clearColor[2], 1.0f }};

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass        = vk.renderPass;
        rpBegin.framebuffer       = vk.framebuffers[imageIndex];
        rpBegin.renderArea.extent = vk.swapExtent;
        rpBegin.clearValueCount   = 1;
        rpBegin.pClearValues      = &clear;

        vkCmdBeginRenderPass(vk.cmdBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vk.cmdBuffer);
        vkCmdEndRenderPass(vk.cmdBuffer);
        vkEndCommandBuffer(vk.cmdBuffer);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &vk.imageAvailable;
        submit.pWaitDstStageMask    = &waitStage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &vk.cmdBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &vk.renderFinished;
        vkQueueSubmit(vk.graphicsQueue, 1, &submit, vk.inFlight);

        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &vk.renderFinished;
        present.swapchainCount     = 1;
        present.pSwapchains        = &vk.swapchain;
        present.pImageIndices      = &imageIndex;
        vkQueuePresentKHR(vk.graphicsQueue, &present);
    }

    // Cleanup
    cleanupVulkan(vk);
    SDL_DestroyWindow(window);
    SDL_Quit();
    FP_CORE_INFO("Engine shutdown cleanly ✓");
    return 0;
}