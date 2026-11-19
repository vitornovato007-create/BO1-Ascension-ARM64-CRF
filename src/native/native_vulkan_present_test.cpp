// native_vulkan_present_test.cpp - one-frame Vulkan WSI diagnostic for Android/SDL2.
// This deliberately does not create a game renderer or change the active DXVK/GLES implementation.
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef __ANDROID__

namespace
{
std::string s_log;

const char *ResultName(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
    default: return "VK_RESULT_UNKNOWN";
    }
}

void Log(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    s_log += line;
    fputs(line, stderr);
    fflush(stderr);
}

bool HasDeviceExtension(VkPhysicalDevice physical, const char *wanted)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkExtensionProperties> extensions(count);
    if (count && vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()) != VK_SUCCESS)
        return false;
    for (const VkExtensionProperties &extension : extensions)
        if (strcmp(extension.extensionName, wanted) == 0)
            return true;
    return false;
}

struct State
{
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
};

void Destroy(State &s)
{
    if (s.device)
        vkDeviceWaitIdle(s.device);
    for (VkFramebuffer framebuffer : s.framebuffers)
        if (framebuffer) vkDestroyFramebuffer(s.device, framebuffer, nullptr);
    if (s.renderPass) vkDestroyRenderPass(s.device, s.renderPass, nullptr);
    for (VkImageView view : s.views)
        if (view) vkDestroyImageView(s.device, view, nullptr);
    if (s.acquired) vkDestroySemaphore(s.device, s.acquired, nullptr);
    if (s.rendered) vkDestroySemaphore(s.device, s.rendered, nullptr);
    if (s.fence) vkDestroyFence(s.device, s.fence, nullptr);
    if (s.commandPool) vkDestroyCommandPool(s.device, s.commandPool, nullptr);
    if (s.swapchain) vkDestroySwapchainKHR(s.device, s.swapchain, nullptr);
    if (s.device) vkDestroyDevice(s.device, nullptr);
    if (s.surface && s.instance) vkDestroySurfaceKHR(s.instance, s.surface, nullptr);
    if (s.instance) vkDestroyInstance(s.instance, nullptr);
}

bool Fail(const char *stage, VkResult result)
{
    Log("native-vk-present: FAIL stage=%s result=%d (%s)\n", stage, (int)result, ResultName(result));
    return false;
}
}

extern "C" int bo1_native_vulkan_present_test(SDL_Window *window)
{
    s_log.clear();
    Log("\nBO1 Vulkan native presentation test\n");
    Log("===================================\n");
    Log("native-vk-present: SDL window=%s\n", window ? "valid" : "null");
    if (!window)
    {
        Log("native-vk-present: FAIL stage=window result=SDL window is null\n");
        return 0;
    }

    State s;
    uint32_t extensionCount = 0;
    // SDL2 uses a two-call pattern: query the count, then fill caller-owned
    // storage. Passing &extensionNames here would be const char*** and is
    // incompatible with SDL_Vulkan_GetInstanceExtensions.
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr))
    {
        Log("native-vk-present: FAIL stage=SDL_Vulkan_GetInstanceExtensions(count) error=%s\\n", SDL_GetError());
        return 0;
    }

    std::vector<const char *> extensionNames(extensionCount);
    if (extensionCount > 0 &&
        !SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensionNames.data()))
    {
        Log("native-vk-present: FAIL stage=SDL_Vulkan_GetInstanceExtensions(names) error=%s\\n", SDL_GetError());
        return 0;
    }
    Log("native-vk-present: SDL required instance extensions=%u\\n", extensionCount);

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "BO1 Mali Vulkan presentation test";
    app.applicationVersion = 1;
    app.pEngineName = "BO1 native diagnostics";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledExtensionCount = extensionCount;
    instanceInfo.ppEnabledExtensionNames = extensionNames.empty() ? nullptr : extensionNames.data();
    VkResult result = vkCreateInstance(&instanceInfo, nullptr, &s.instance);
    if (result != VK_SUCCESS)
    {
        Fail("vkCreateInstance", result);
        return 0;
    }
    Log("native-vk-present: PASS stage=vkCreateInstance api=1.1\n");

    if (!SDL_Vulkan_CreateSurface(window, s.instance, &s.surface))
    {
        Log("native-vk-present: FAIL stage=SDL_Vulkan_CreateSurface error=%s\n", SDL_GetError());
        Destroy(s);
        return 0;
    }
    Log("native-vk-present: PASS stage=Android SDL Vulkan surface creation\n");

    uint32_t physicalCount = 0;
    result = vkEnumeratePhysicalDevices(s.instance, &physicalCount, nullptr);
    if (result != VK_SUCCESS || physicalCount == 0)
    {
        Fail("vkEnumeratePhysicalDevices", result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result);
        Destroy(s);
        return 0;
    }
    std::vector<VkPhysicalDevice> physicals(physicalCount);
    result = vkEnumeratePhysicalDevices(s.instance, &physicalCount, physicals.data());
    if (result != VK_SUCCESS)
    {
        Fail("vkEnumeratePhysicalDevices(data)", result);
        Destroy(s);
        return 0;
    }

    for (VkPhysicalDevice physical : physicals)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        Log("native-vk-present: candidate GPU=%s vendor=0x%04x device=0x%04x api=%u.%u.%u\n",
            properties.deviceName, properties.vendorID, properties.deviceID,
            VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
            VK_VERSION_PATCH(properties.apiVersion));
        if (!HasDeviceExtension(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            Log("native-vk-present: candidate rejected: VK_KHR_swapchain unavailable\n");
            continue;
        }

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        if (familyCount)
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());

        for (uint32_t family = 0; family < familyCount; ++family)
        {
            if (families[family].queueCount == 0 ||
                (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
                continue;
            VkBool32 canPresent = VK_FALSE;
            result = vkGetPhysicalDeviceSurfaceSupportKHR(physical, family, s.surface, &canPresent);
            if (result == VK_SUCCESS && canPresent)
            {
                s.physical = physical;
                s.queueFamily = family;
                break;
            }
            Log("native-vk-present: queue[%u] graphics=yes present=%s result=%d (%s)\n",
                family, canPresent ? "yes" : "no", (int)result, ResultName(result));
        }
        if (s.physical)
            break;
    }
    if (!s.physical)
    {
        Log("native-vk-present: FAIL stage=physical-device-selection no graphics+present queue with swapchain support\n");
        Destroy(s);
        return 0;
    }

    VkPhysicalDeviceProperties selectedProperties{};
    VkPhysicalDeviceFeatures availableFeatures{};
    vkGetPhysicalDeviceProperties(s.physical, &selectedProperties);
    vkGetPhysicalDeviceFeatures(s.physical, &availableFeatures);
    Log("native-vk-present: selected GPU=%s queueFamily=%u\n", selectedProperties.deviceName, s.queueFamily);
    Log("native-vk-present: optional features samplerAnisotropy=%u textureCompressionBC=%u occlusionQueryPrecise=%u\n",
        availableFeatures.samplerAnisotropy, availableFeatures.textureCompressionBC, availableFeatures.occlusionQueryPrecise);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = s.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char *deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    // No optional features are enabled: the test needs only core graphics and presentation.
    result = vkCreateDevice(s.physical, &deviceInfo, nullptr, &s.device);
    if (result != VK_SUCCESS)
    {
        Fail("vkCreateDevice", result);
        Destroy(s);
        return 0;
    }
    vkGetDeviceQueue(s.device, s.queueFamily, 0, &s.queue);
    Log("native-vk-present: PASS stage=vkCreateDevice queue=%p\n", (void *)s.queue);

    VkSurfaceCapabilitiesKHR capabilities{};
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s.physical, s.surface, &capabilities);
    if (result != VK_SUCCESS)
    {
        Fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
        Destroy(s);
        return 0;
    }
    Log("native-vk-present: surface minImages=%u maxImages=%u usage=0x%x currentExtent=%ux%u\n",
        capabilities.minImageCount, capabilities.maxImageCount, capabilities.supportedUsageFlags,
        capabilities.currentExtent.width, capabilities.currentExtent.height);
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
    {
        Log("native-vk-present: FAIL stage=surface-capabilities color-attachment usage unsupported\n");
        Destroy(s);
        return 0;
    }

    uint32_t formatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(s.physical, s.surface, &formatCount, nullptr);
    if (result != VK_SUCCESS || formatCount == 0)
    {
        Fail("vkGetPhysicalDeviceSurfaceFormatsKHR", result == VK_SUCCESS ? VK_ERROR_FORMAT_NOT_SUPPORTED : result);
        Destroy(s);
        return 0;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(s.physical, s.surface, &formatCount, formats.data());
    if (result != VK_SUCCESS)
    {
        Fail("vkGetPhysicalDeviceSurfaceFormatsKHR(data)", result);
        Destroy(s);
        return 0;
    }
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    {
        surfaceFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
        surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
    else
    {
        for (const VkSurfaceFormatKHR &format : formats)
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                surfaceFormat = format;
                break;
            }
    }
    Log("native-vk-present: chosen surface format=%d colorSpace=%d\n",
        (int)surfaceFormat.format, (int)surfaceFormat.colorSpace);

    uint32_t modeCount = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(s.physical, s.surface, &modeCount, nullptr);
    if (result != VK_SUCCESS || modeCount == 0)
    {
        Fail("vkGetPhysicalDeviceSurfacePresentModesKHR", result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result);
        Destroy(s);
        return 0;
    }
    std::vector<VkPresentModeKHR> modes(modeCount);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(s.physical, s.surface, &modeCount, modes.data());
    if (result != VK_SUCCESS)
    {
        Fail("vkGetPhysicalDeviceSurfacePresentModesKHR(data)", result);
        Destroy(s);
        return 0;
    }
    bool fifoAvailable = false;
    for (VkPresentModeKHR mode : modes)
        fifoAvailable |= mode == VK_PRESENT_MODE_FIFO_KHR;
    if (!fifoAvailable)
    {
        Log("native-vk-present: FAIL stage=present-modes FIFO mode missing\n");
        Destroy(s);
        return 0;
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX || extent.height == UINT32_MAX)
    {
        int drawableWidth = 0, drawableHeight = 0;
        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        if (drawableWidth <= 0 || drawableHeight <= 0)
        {
            Log("native-vk-present: FAIL stage=drawable-size size=%dx%d\n", drawableWidth, drawableHeight);
            Destroy(s);
            return 0;
        }
        extent.width = std::max(capabilities.minImageExtent.width,
            std::min(capabilities.maxImageExtent.width, (uint32_t)drawableWidth));
        extent.height = std::max(capabilities.minImageExtent.height,
            std::min(capabilities.maxImageExtent.height, (uint32_t)drawableHeight));
    }
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && imageCount > capabilities.maxImageCount)
        imageCount = capabilities.maxImageCount;
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha & compositeAlpha) == 0)
    {
        const VkCompositeAlphaFlagBitsKHR choices[] = {
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
        };
        bool found = false;
        for (VkCompositeAlphaFlagBitsKHR choice : choices)
            if (capabilities.supportedCompositeAlpha & choice)
            {
                compositeAlpha = choice;
                found = true;
                break;
            }
        if (!found)
        {
            Log("native-vk-present: FAIL stage=composite-alpha no supported mode\n");
            Destroy(s);
            return 0;
        }
    }

    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = s.surface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = surfaceFormat.format;
    swapInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapInfo.imageExtent = extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = capabilities.currentTransform;
    swapInfo.compositeAlpha = compositeAlpha;
    swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapInfo.clipped = VK_TRUE;
    result = vkCreateSwapchainKHR(s.device, &swapInfo, nullptr, &s.swapchain);
    if (result != VK_SUCCESS)
    {
        Fail("vkCreateSwapchainKHR", result);
        Destroy(s);
        return 0;
    }
    Log("native-vk-present: PASS stage=vkCreateSwapchainKHR extent=%ux%u imagesRequested=%u\n",
        extent.width, extent.height, imageCount);

    uint32_t swapImageCount = 0;
    result = vkGetSwapchainImagesKHR(s.device, s.swapchain, &swapImageCount, nullptr);
    if (result != VK_SUCCESS || swapImageCount == 0)
    {
        Fail("vkGetSwapchainImagesKHR", result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result);
        Destroy(s);
        return 0;
    }
    s.images.resize(swapImageCount);
    result = vkGetSwapchainImagesKHR(s.device, s.swapchain, &swapImageCount, s.images.data());
    if (result != VK_SUCCESS)
    {
        Fail("vkGetSwapchainImagesKHR(data)", result);
        Destroy(s);
        return 0;
    }

    VkAttachmentDescription attachment{};
    attachment.format = surfaceFormat.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    result = vkCreateRenderPass(s.device, &renderPassInfo, nullptr, &s.renderPass);
    if (result != VK_SUCCESS)
    {
        Fail("vkCreateRenderPass", result);
        Destroy(s);
        return 0;
    }

    s.views.resize(swapImageCount, VK_NULL_HANDLE);
    s.framebuffers.resize(swapImageCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < swapImageCount; ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = s.images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        result = vkCreateImageView(s.device, &viewInfo, nullptr, &s.views[i]);
        if (result != VK_SUCCESS)
        {
            Fail("vkCreateImageView", result);
            Destroy(s);
            return 0;
        }
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = s.renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &s.views[i];
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;
        result = vkCreateFramebuffer(s.device, &framebufferInfo, nullptr, &s.framebuffers[i]);
        if (result != VK_SUCCESS)
        {
            Fail("vkCreateFramebuffer", result);
            Destroy(s);
            return 0;
        }
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = s.queueFamily;
    result = vkCreateCommandPool(s.device, &poolInfo, nullptr, &s.commandPool);
    if (result != VK_SUCCESS)
    {
        Fail("vkCreateCommandPool", result);
        Destroy(s);
        return 0;
    }
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = s.commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(s.device, &commandInfo, &s.commandBuffer);
    if (result != VK_SUCCESS)
    {
        Fail("vkAllocateCommandBuffers", result);
        Destroy(s);
        return 0;
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    result = vkCreateSemaphore(s.device, &semaphoreInfo, nullptr, &s.acquired);
    if (result != VK_SUCCESS)
    {
        Fail("vkCreateSemaphore(acquired)", result);
        Destroy(s);
        return 0;
    }
    result = vkCreateSemaphore(s.device, &semaphoreInfo, nullptr, &s.rendered);
    if (result != VK_SUCCESS)
    {
        Fail("vkCreateSemaphore(rendered)", result);
        Destroy(s);
        return 0;
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    result = vkCreateFence(s.device, &fenceInfo, nullptr, &s.fence);
    if (result != VK_SUCCESS)
    {
        Fail("vkCreateFence", result);
        Destroy(s);
        return 0;
    }

    result = vkWaitForFences(s.device, 1, &s.fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS)
    {
        Fail("vkWaitForFences(before-acquire)", result);
        Destroy(s);
        return 0;
    }
    uint32_t imageIndex = 0;
    result = vkAcquireNextImageKHR(s.device, s.swapchain, UINT64_MAX, s.acquired, VK_NULL_HANDLE, &imageIndex);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        Fail("vkAcquireNextImageKHR", result);
        Destroy(s);
        return 0;
    }
    Log("native-vk-present: PASS stage=vkAcquireNextImageKHR image=%u\n", imageIndex);

    result = vkResetFences(s.device, 1, &s.fence);
    if (result != VK_SUCCESS)
    {
        Fail("vkResetFences", result);
        Destroy(s);
        return 0;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(s.commandBuffer, &beginInfo);
    if (result != VK_SUCCESS)
    {
        Fail("vkBeginCommandBuffer", result);
        Destroy(s);
        return 0;
    }
    VkClearValue clear{};
    clear.color.float32[0] = 0.035f;
    clear.color.float32[1] = 0.18f;
    clear.color.float32[2] = 0.42f;
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo renderBegin{};
    renderBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderBegin.renderPass = s.renderPass;
    renderBegin.framebuffer = s.framebuffers[imageIndex];
    renderBegin.renderArea.extent = extent;
    renderBegin.clearValueCount = 1;
    renderBegin.pClearValues = &clear;
    vkCmdBeginRenderPass(s.commandBuffer, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(s.commandBuffer);
    result = vkEndCommandBuffer(s.commandBuffer);
    if (result != VK_SUCCESS)
    {
        Fail("vkEndCommandBuffer", result);
        Destroy(s);
        return 0;
    }
    Log("native-vk-present: PASS stage=record-clear-command-buffer\n");

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &s.acquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &s.commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &s.rendered;
    result = vkQueueSubmit(s.queue, 1, &submit, s.fence);
    if (result != VK_SUCCESS)
    {
        Fail("vkQueueSubmit", result);
        Destroy(s);
        return 0;
    }
    result = vkWaitForFences(s.device, 1, &s.fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS)
    {
        Fail("vkWaitForFences(after-submit)", result);
        Destroy(s);
        return 0;
    }
    Log("native-vk-present: PASS stage=vkQueueSubmit\n");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &s.rendered;
    present.swapchainCount = 1;
    present.pSwapchains = &s.swapchain;
    present.pImageIndices = &imageIndex;
    result = vkQueuePresentKHR(s.queue, &present);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        Fail("vkQueuePresentKHR", result);
        Destroy(s);
        return 0;
    }
    Log("native-vk-present: PASS stage=vkQueuePresentKHR result=%d (%s)\n", (int)result, ResultName(result));
    result = vkQueueWaitIdle(s.queue);
    if (result != VK_SUCCESS)
    {
        Fail("vkQueueWaitIdle", result);
        Destroy(s);
        return 0;
    }
    Log("native-vk-present: PASS stage=queue-idle; one clear frame was presented\n");
    SDL_Delay(900);
    Log("native-vk-present: SUCCESS GPU=%s extent=%ux%u\n",
        selectedProperties.deviceName, extent.width, extent.height);
    Destroy(s);
    return 1;
}

extern "C" const char *bo1_native_vulkan_present_test_log(void)
{
    return s_log.c_str();
}
#else

namespace
{
std::string s_log = "native-vk-present: SKIP this presentation test is Android-only\n";
}

extern "C" int bo1_native_vulkan_present_test(SDL_Window *)
{
    return 0;
}

extern "C" const char *bo1_native_vulkan_present_test_log(void)
{
    return s_log.c_str();
}

#endif
