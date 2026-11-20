// native_vulkan_renderer.h - persistent Vulkan WSI renderer bring-up API.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Takes an SDL_Window created with SDL_WINDOW_VULKAN. The caller retains window ownership.
// Initializes and retains VkInstance, VkSurfaceKHR, VkDevice, swapchain and frame resources.
int bo1_native_vulkan_renderer_init(void *sdl_window);

// Acquires, clears and presents one swapchain image. RGB components are in [0, 1].
// Handles drawable-size changes and VK_ERROR_OUT_OF_DATE_KHR by rebuilding swapchain resources.
// This remains a WSI clear-pass foundation, not yet the game's D3D9 renderer.
int bo1_native_vulkan_renderer_present(float red, float green, float blue);

// Waits for the device and releases every Vulkan object owned by this module.
void bo1_native_vulkan_renderer_shutdown(void);

#ifdef __cplusplus
}
#endif
