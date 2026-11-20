# Native Vulkan renderer — V0 implementation status

This branch now contains the first persistent Vulkan WSI backend foundation:

- `src/native/native_vulkan_renderer.h`
- `src/native/native_vulkan_renderer.cpp`

## What is implemented

- Android-only Vulkan implementation; desktop/Linux builds receive inert stubs until their Vulkan loader configuration is explicit.
- Creates and retains a Vulkan instance, SDL surface, selected physical device, graphics/present queue, logical device and swapchain.
- Creates swapchain image views, a clear render pass, framebuffers, command pool/buffer, semaphores and a fence.
- Exposes init, one-frame clear/present, and shutdown entry points.
- Uses conservative single-frame synchronization and waits for the queue to become idle. This is intentionally a bring-up path, not a performance configuration.
- Leaves the existing DXVK bridge and GLES D3D9 shim untouched.

## Current limits — do not mistake this for BO1 native rendering

This is a persistent WSI/clear-pass backend, not yet the game's D3D9 renderer. It does not draw a triangle, compile game shaders, translate D3D9 state, upload BO1 textures, or route game draw calls through Vulkan. It is also not yet wired into the game's video selection/runtime loop; the API is available for the next integration step.

Swapchain recreation on resize/out-of-date is not implemented yet. The caller must provide an SDL window created with `SDL_WINDOW_VULKAN`, keep it alive while this backend is active, and call shutdown before destroying that window.

## Next implementation steps

1. Add explicit opt-in runtime selection and connect init/present/shutdown to the native video lifecycle without changing the DXVK default.
2. Add swapchain recreation and robust Android pause/resume handling.
3. Add a minimal SPIR-V graphics pipeline and triangle test.
4. Begin D3D9 resource/state translation, descriptor management, shader translation and texture-format fallback (including software DXT decode on Mali-G76).
5. Keep the DXVK path available until a complete BO1 map renders through the native Vulkan backend.

No workflow or build was started as part of this change. Compilation and device testing remain pending.
