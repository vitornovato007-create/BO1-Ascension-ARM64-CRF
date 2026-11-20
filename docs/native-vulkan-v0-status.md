# Native Vulkan renderer — V0.2 implementation status

The native Vulkan foundation has advanced from a one-time WSI clear test to a persistent Android swapchain owner with resize/out-of-date recovery.

## Implemented

- Android-only Vulkan implementation; non-Android builds retain inert stubs until a desktop Vulkan loader path is configured.
- Creates and retains the Vulkan instance, SDL surface, selected physical device, graphics/present queue, logical device and swapchain.
- Creates swapchain image views, a clear render pass, framebuffers, command pool/buffer, semaphores and a fence.
- Exposes initialization, clear/present and shutdown entry points.
- Retains the SDL window pointer for the lifetime of the renderer; the caller must keep the window alive until shutdown.
- Detects drawable-size changes before presenting and rebuilds swapchain-dependent resources.
- Handles `VK_ERROR_OUT_OF_DATE_KHR` from image acquisition by scheduling recreation, and handles `VK_SUBOPTIMAL_KHR` / out-of-date presentation by rebuilding on the next frame.
- Handles temporary zero-sized drawable surfaces without submitting work.
- Uses a fence for command-buffer reuse and one image-acquired semaphore for the single in-flight frame. Each swapchain image owns its own present-wait semaphore; that semaphore is reused only after the same image is reacquired. The previous per-frame `vkQueueWaitIdle` stall was removed. The renderer still permits only one frame in flight, so this is an initial synchronization improvement rather than a complete multi-frame scheduler.
- Existing DXVK and GLES D3D9 paths remain unchanged.

## Explicit limitations

This is **not yet BO1 native rendering**. It still clears the swapchain to a solid color. It does not create graphics pipelines or SPIR-V shader modules, draw geometry, translate D3D9 state, upload game textures, or route BO1 draw calls through Vulkan. It is not connected to the game's D3D9 device lifecycle. The launcher continues to block native-Vulkan game startup rather than silently using DXVK or GLES.

The renderer requires an SDL window created with `SDL_WINDOW_VULKAN`. The caller owns the window and must call renderer shutdown before destroying it.

## Next engineering milestones

1. Add an isolated Vulkan bring-up test path that invokes init/present/shutdown from the native video lifecycle, without claiming to render BO1.
2. Replace the clear-only pass with a minimal SPIR-V graphics pipeline and triangle test to verify vertex/fragment shader execution on Mali.
3. Add Vulkan buffer/image allocation, staging uploads, descriptors and device-memory tracking.
4. Implement BC1/BC2/BC3 software decode fallback because the Galaxy S10 Mali-G76 probe reported BC1 and BC3 unsupported.
5. Implement the D3D9-to-Vulkan frontend: shader bytecode translation to SPIR-V, D3D9 state/resource mapping, render targets and draw calls.
6. Enable BO1 startup through this backend only after the first real D3D9 scene renders; keep DXVK selectable as the working alternative.

No GitHub Actions workflow or build was started. Compilation and device testing remain pending.
