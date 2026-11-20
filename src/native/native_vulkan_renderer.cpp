// native_vulkan_renderer.cpp - persistent Vulkan WSI backend (V0 clear-pass foundation).
// This module is deliberately independent of DXVK and the GLES D3D9 shim. It owns its Vulkan objects
// for the lifetime of the renderer and can be advanced toward the native D3D9 frontend.
#include "native_vulkan_renderer.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef __ANDROID__
namespace
{
struct Renderer
{
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = UINT32_MAX;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    SDL_Window *window = nullptr;
    bool recreateSwapchain = false;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    bool initialized = false;
};

Renderer g;

void Log(const char *message, VkResult result = VK_SUCCESS)
{
    if (result == VK_SUCCESS)
        fprintf(stderr, "native-vk-renderer: %s\n", message);
    else
        fprintf(stderr, "native-vk-renderer: %s (VkResult=%d)\n", message, (int)result);
}

bool HasSwapchain(VkPhysicalDevice physical)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkExtensionProperties> props(count);
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, props.data()) != VK_SUCCESS)
        return false;
    for (const auto &p : props)
        if (strcmp(p.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            return true;
    return false;
}

void DestroySwapchainResources()
{
    if (!g.device) return;
    for (VkFramebuffer f : g.framebuffers)
        if (f) vkDestroyFramebuffer(g.device, f, nullptr);
    g.framebuffers.clear();
    if (g.renderPass) vkDestroyRenderPass(g.device, g.renderPass, nullptr);
    g.renderPass = VK_NULL_HANDLE;
    for (VkImageView v : g.views)
        if (v) vkDestroyImageView(g.device, v, nullptr);
    g.views.clear();
    g.images.clear();
    if (g.swapchain) vkDestroySwapchainKHR(g.device, g.swapchain, nullptr);
    g.swapchain = VK_NULL_HANDLE;
    g.format = VK_FORMAT_UNDEFINED;
    g.extent = {};
}

void Destroy()
{
    if (g.device)
        vkDeviceWaitIdle(g.device);
    DestroySwapchainResources();
    if (g.acquired) vkDestroySemaphore(g.device, g.acquired, nullptr);
    if (g.rendered) vkDestroySemaphore(g.device, g.rendered, nullptr);
    if (g.fence) vkDestroyFence(g.device, g.fence, nullptr);
    if (g.commandPool) vkDestroyCommandPool(g.device, g.commandPool, nullptr);
    if (g.device) vkDestroyDevice(g.device, nullptr);
    if (g.surface && g.instance) vkDestroySurfaceKHR(g.instance, g.surface, nullptr);
    if (g.instance) vkDestroyInstance(g.instance, nullptr);
    g = Renderer{};
}

bool CreateSwapchain(SDL_Window *window)
{
    DestroySwapchainResources();
    VkSurfaceCapabilitiesKHR caps{};
    VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.physical, g.surface, &caps);
    if (r != VK_SUCCESS) { Log("surface capabilities failed", r); return false; }

    uint32_t count = 0;
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(g.physical, g.surface, &count, nullptr);
    if (r != VK_SUCCESS || count == 0) { Log("surface formats unavailable", r); return false; }
    std::vector<VkSurfaceFormatKHR> formats(count);
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(g.physical, g.surface, &count, formats.data());
    if (r != VK_SUCCESS) { Log("surface format query failed", r); return false; }
    VkSurfaceFormatKHR chosen = formats[0];
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
        chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
    for (const auto &f : formats)
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
    g.format = chosen.format;

    int dw = 0, dh = 0;
    SDL_Vulkan_GetDrawableSize(window, &dw, &dh);
    if (dw <= 0 || dh <= 0) { Log("drawable size is zero"); return false; }
    g.extent = caps.currentExtent;
    if (g.extent.width == UINT32_MAX)
    {
        g.extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, (uint32_t)dw));
        g.extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, (uint32_t)dh));
    }
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
    { Log("surface does not support color-attachment swapchain images"); return false; }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = g.surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = g.extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & ci.compositeAlpha))
    {
        const VkCompositeAlphaFlagBitsKHR modes[] = { VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR };
        bool found = false;
        for (auto mode : modes) if (caps.supportedCompositeAlpha & mode) { ci.compositeAlpha = mode; found = true; break; }
        if (!found) { Log("no supported composite-alpha mode"); return false; }
    }
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    r = vkCreateSwapchainKHR(g.device, &ci, nullptr, &g.swapchain);
    if (r != VK_SUCCESS) { Log("vkCreateSwapchainKHR failed", r); return false; }

    r = vkGetSwapchainImagesKHR(g.device, g.swapchain, &count, nullptr);
    if (r != VK_SUCCESS || !count) { Log("swapchain image query failed", r); return false; }
    g.images.resize(count);
    r = vkGetSwapchainImagesKHR(g.device, g.swapchain, &count, g.images.data());
    if (r != VK_SUCCESS) { Log("swapchain image retrieval failed", r); return false; }
    g.views.resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = g.images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = g.format;
        vi.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        r = vkCreateImageView(g.device, &vi, nullptr, &g.views[i]);
        if (r != VK_SUCCESS) { Log("vkCreateImageView failed", r); return false; }
    }

    VkAttachmentDescription attachment{};
    attachment.format = g.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1; rp.pAttachments = &attachment;
    rp.subpassCount = 1; rp.pSubpasses = &subpass;
    rp.dependencyCount = 1; rp.pDependencies = &dep;
    r = vkCreateRenderPass(g.device, &rp, nullptr, &g.renderPass);
    if (r != VK_SUCCESS) { Log("vkCreateRenderPass failed", r); return false; }

    g.framebuffers.resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = g.renderPass;
        fi.attachmentCount = 1; fi.pAttachments = &g.views[i];
        fi.width = g.extent.width; fi.height = g.extent.height; fi.layers = 1;
        r = vkCreateFramebuffer(g.device, &fi, nullptr, &g.framebuffers[i]);
        if (r != VK_SUCCESS) { Log("vkCreateFramebuffer failed", r); return false; }
    }
    return true;
}
}

extern "C" int bo1_native_vulkan_renderer_init(void *sdlWindow)
{
    if (g.initialized) return 1;
    SDL_Window *window = static_cast<SDL_Window *>(sdlWindow);
    if (!window) { Log("init rejected: SDL window is null"); return 0; }
    g.window = window;
    uint32_t extCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr)) { Log(SDL_GetError()); return 0; }
    std::vector<const char *> extensions(extCount);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, extensions.data())) { Log(SDL_GetError()); return 0; }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "BO1 Native Vulkan";
    app.applicationVersion = 1;
    app.pEngineName = "BO1";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ii.pApplicationInfo = &app;
    ii.enabledExtensionCount = extCount;
    ii.ppEnabledExtensionNames = extensions.data();
    VkResult r = vkCreateInstance(&ii, nullptr, &g.instance);
    if (r != VK_SUCCESS) { Log("vkCreateInstance failed", r); Destroy(); return 0; }
    if (!SDL_Vulkan_CreateSurface(window, g.instance, &g.surface)) { Log(SDL_GetError()); Destroy(); return 0; }

    uint32_t physicalCount = 0;
    r = vkEnumeratePhysicalDevices(g.instance, &physicalCount, nullptr);
    if (r != VK_SUCCESS || !physicalCount) { Log("no Vulkan physical devices", r); Destroy(); return 0; }
    std::vector<VkPhysicalDevice> devices(physicalCount);
    r = vkEnumeratePhysicalDevices(g.instance, &physicalCount, devices.data());
    if (r != VK_SUCCESS) { Log("physical device enumeration failed", r); Destroy(); return 0; }
    for (VkPhysicalDevice p : devices)
    {
        if (!HasSwapchain(p)) continue;
        uint32_t n = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(p, &n, nullptr);
        std::vector<VkQueueFamilyProperties> q(n);
        vkGetPhysicalDeviceQueueFamilyProperties(p, &n, q.data());
        for (uint32_t i = 0; i < n; ++i)
        {
            VkBool32 present = VK_FALSE;
            if (q[i].queueCount && (q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                vkGetPhysicalDeviceSurfaceSupportKHR(p, i, g.surface, &present) == VK_SUCCESS && present)
            { g.physical = p; g.family = i; break; }
        }
        if (g.physical) break;
    }
    if (!g.physical) { Log("no graphics+present queue with VK_KHR_swapchain"); Destroy(); return 0; }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = g.family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
    const char *deviceExt[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = deviceExt;
    r = vkCreateDevice(g.physical, &di, nullptr, &g.device);
    if (r != VK_SUCCESS) { Log("vkCreateDevice failed", r); Destroy(); return 0; }
    vkGetDeviceQueue(g.device, g.family, 0, &g.queue);
    if (!CreateSwapchain(window)) { Destroy(); return 0; }

    VkCommandPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = g.family;
    r = vkCreateCommandPool(g.device, &pi, nullptr, &g.commandPool);
    if (r != VK_SUCCESS) { Log("vkCreateCommandPool failed", r); Destroy(); return 0; }
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g.commandPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(g.device, &ai, &g.command);
    if (r != VK_SUCCESS) { Log("vkAllocateCommandBuffers failed", r); Destroy(); return 0; }
    VkSemaphoreCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(g.device, &si, nullptr, &g.acquired) != VK_SUCCESS ||
        vkCreateSemaphore(g.device, &si, nullptr, &g.rendered) != VK_SUCCESS)
    { Log("semaphore creation failed"); Destroy(); return 0; }
    VkFenceCreateInfo fence{}; fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(g.device, &fence, nullptr, &g.fence) != VK_SUCCESS)
    { Log("fence creation failed"); Destroy(); return 0; }
    g.initialized = true;
    VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(g.physical, &props);
    fprintf(stderr, "native-vk-renderer: initialized GPU=%s extent=%ux%u images=%zu frames=1\n",
        props.deviceName, g.extent.width, g.extent.height, g.images.size());
    return 1;
}

extern "C" int bo1_native_vulkan_renderer_present(float red, float green, float blue)
{
    if (!g.initialized || !g.window) return 0;

    int drawableWidth = 0, drawableHeight = 0;
    SDL_Vulkan_GetDrawableSize(g.window, &drawableWidth, &drawableHeight);
    // Android can temporarily report a zero-sized surface while the app is paused or its window is recreated.
    if (drawableWidth <= 0 || drawableHeight <= 0)
        return 1;

    const bool extentChanged = (uint32_t)drawableWidth != g.extent.width ||
        (uint32_t)drawableHeight != g.extent.height;
    if (g.recreateSwapchain || extentChanged)
    {
        VkResult idle = vkDeviceWaitIdle(g.device);
        if (idle != VK_SUCCESS) { Log("device idle before swapchain recreation failed", idle); return 0; }
        if (!CreateSwapchain(g.window)) { Log("swapchain recreation failed"); return 0; }
        g.recreateSwapchain = false;
        fprintf(stderr, "native-vk-renderer: swapchain recreated extent=%ux%u images=%zu\n",
            g.extent.width, g.extent.height, g.images.size());
    }

    VkResult r = vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS) { Log("frame fence wait failed", r); return 0; }
    uint32_t index = 0;
    r = vkAcquireNextImageKHR(g.device, g.swapchain, UINT64_MAX, g.acquired, VK_NULL_HANDLE, &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        g.recreateSwapchain = true;
        return 1; // the next frame rebuilds the swapchain after waiting for the device
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { Log("image acquisition failed", r); return 0; }
    const bool acquiredSuboptimal = r == VK_SUBOPTIMAL_KHR;
    r = vkResetFences(g.device, 1, &g.fence);
    if (r != VK_SUCCESS) { Log("fence reset failed", r); Destroy(); return 0; }
    r = vkResetCommandBuffer(g.command, 0);
    if (r != VK_SUCCESS) { Log("command buffer reset failed", r); Destroy(); return 0; }
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    r = vkBeginCommandBuffer(g.command, &bi);
    if (r != VK_SUCCESS) { Log("command buffer begin failed", r); Destroy(); return 0; }
    VkClearValue clear{}; clear.color.float32[0] = red; clear.color.float32[1] = green;
    clear.color.float32[2] = blue; clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo rp{}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g.renderPass; rp.framebuffer = g.framebuffers[index];
    rp.renderArea.extent = g.extent; rp.clearValueCount = 1; rp.pClearValues = &clear;
    vkCmdBeginRenderPass(g.command, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(g.command);
    r = vkEndCommandBuffer(g.command);
    if (r != VK_SUCCESS) { Log("command buffer end failed", r); Destroy(); return 0; }
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &g.acquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &g.command;
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &g.rendered;
    r = vkQueueSubmit(g.queue, 1, &submit, g.fence);
    if (r != VK_SUCCESS) { Log("queue submit failed", r); Destroy(); return 0; }
    VkPresentInfoKHR present{}; present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1; present.pWaitSemaphores = &g.rendered;
    present.swapchainCount = 1; present.pSwapchains = &g.swapchain; present.pImageIndices = &index;
    r = vkQueuePresentKHR(g.queue, &present);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR && r != VK_ERROR_OUT_OF_DATE_KHR)
    { Log("queue present failed", r); Destroy(); return 0; }
    // Keep the single-frame synchronization conservative until per-image present semaphores are introduced.
    // This avoids re-signalling a binary semaphore before the presentation engine has consumed its wait.
    VkResult presentIdle = vkQueueWaitIdle(g.queue);
    if (presentIdle != VK_SUCCESS) { Log("queue idle failed", presentIdle); Destroy(); return 0; }
    if (acquiredSuboptimal || r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_OUT_OF_DATE_KHR)
        g.recreateSwapchain = true;
    return 1;
}

extern "C" void bo1_native_vulkan_renderer_shutdown(void)
{
    Destroy();
}

#else

// The first native backend milestone is Android-only. Keep Linux/desktop native builds linkable
// until their Vulkan loader and SDL video backend are configured explicitly.
extern "C" int bo1_native_vulkan_renderer_init(void *) { return 0; }
extern "C" int bo1_native_vulkan_renderer_present(float, float, float) { return 0; }
extern "C" void bo1_native_vulkan_renderer_shutdown(void) {}

#endif // __ANDROID__
