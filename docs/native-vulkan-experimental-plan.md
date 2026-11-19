# Native Vulkan backend: implementation plan

Status: architecture audit and first experimental branch setup. This document does not claim that the game renderer has been ported.

Target branch: `crf/native-vulkan-experimental`
Baseline: `crf/dxvk-manager-experimental`
Device used for the first WSI test: Galaxy S10, Exynos 9820, Mali-G76 MP12, Vulkan 1.1.177.

## Goal

Build an experimental Android renderer that translates the BO1 engine's Direct3D 9 calls directly into Vulkan. The runtime renderer must not load, link, package, or require DXVK. OpenGL ES can remain available as a separate fallback while the native Vulkan backend is developed, but selecting native Vulkan must never silently route through DXVK.

## What the current tests prove

The Android diagnostic path in `src/native/native_vulkan_present_test.cpp` creates a Vulkan instance, SDL Android surface, physical/logical device, swapchain, image view/framebuffer, command buffer, submits a clear, and presents one frame. On the Galaxy S10 it completed through `vkQueuePresentKHR = VK_SUCCESS` at 1520x720.

This validates Vulkan WSI and basic queue submission only. It is not a BO1 renderer: it does not create game textures, translate D3D9 shaders, bind game state, draw game geometry, or render the game's frame.

The user's subsequent test reached the Zombies front end and loaded Kino der Toten with normal-looking textures, but that run used the existing game renderer path. It is not evidence that the native Vulkan renderer is already drawing the game.

## Current rendering architecture found in the source

1. The engine issues Direct3D 9 calls.
2. `src/web/d3d9/d3d9_vkbridge.cpp` implements the D3D9 interfaces by forwarding calls through `src/native/vk/bo1vk.h` to `bo1vk_dxvk.cpp`, which loads DXVK Native's D3D9 library.
3. The independent D3D9 shim in `src/web/d3d9/` currently renders through OpenGL ES 3. Its implementation is split across device, draw, resources and framebuffer code and uses GL state, GL resources and GL shader programs.
4. `src/web/d3d9/d3d9_shader_translate.*` translates D3D9 shader bytecode to GLSL ES 3.00 through MojoShader. That output is not SPIR-V and cannot be passed directly to Vulkan.
5. `src/native/native_video.cpp` currently creates an SDL OpenGL ES window/context for the GLES renderer. The separate Vulkan diagnostic creates a Vulkan surface for its one-frame test; the game window/context lifecycle must be redesigned to support a Vulkan-owned swapchain without creating a GL context.
6. The Android workflow and packaging scripts still build DXVK Native, configure the engine with DXVK headers, and optionally package `libdxvk_d3d9.so`. Those build steps must be removed from the native-Vulkan build profile only after the replacement renderer is linked and tested.

## Required implementation layers

### A. Vulkan platform/device layer

Create a persistent Vulkan platform object owned by the native video layer:
- SDL window with `SDL_WINDOW_VULKAN`, no SDL GL context.
- Instance extensions queried from SDL; Android surface creation and destruction.
- Physical-device selection based on actual required features and queue support, not vendor-name checks.
- Device/queue, surface capabilities, swapchain, image views, synchronization and resize/out-of-date handling.
- Explicit Vulkan 1.1-compatible feature path; do not require Vulkan 1.3 or optional extensions that the Mali-G76 does not expose.
- Correct shutdown/restart ordering for Android activity pause/resume and window replacement.

The one-frame diagnostic should become a reusable WSI/device module, not remain a separate test-only renderer.

### B. D3D9 compatibility frontend

Implement a new backend behind the same engine-visible D3D9 interfaces. Preserve the engine's 32-bit-pointer ABI and keep Vulkan handles opaque across the p32 boundary, as the current DXVK bridge does.

The implementation needs coverage for the D3D9 calls actually used by BO1:
- Device creation, reset, present, swap chains and back buffers.
- Vertex/index buffers, vertex declarations/FVF and stream frequencies.
- 2D/cube/volume textures, surfaces, render targets and depth-stencil resources.
- Lock/unlock, update/copy/stretch, clear and readback operations used by the engine.
- Render states, sampler states, texture stages, transforms, viewport, scissor and constants.
- Vertex/pixel shader creation and binding, plus draw/draw-indexed and UP variants.
- Queries only where the engine requires them; unsupported optional D3D9 features must be logged and handled deterministically.

Do not copy GL calls into a thin wrapper. The backend must own Vulkan images, buffers, memory, descriptor sets, render passes/dynamic-rendering state, pipeline layouts, graphics pipelines and command buffers.

### C. Shader path

The current translator emits GLSL ES 3.00. Native Vulkan requires valid SPIR-V. Add a controlled shader path:
- Keep D3D9 bytecode as the canonical cache key/input.
- Convert the translated shader to Vulkan-compatible GLSL (desktop GLSL 450 semantics) or add a D3D9-bytecode-to-SPIR-V compiler path.
- Compile and validate SPIR-V, reflect vertex inputs, varyings, uniforms and samplers, and build pipeline layouts.
- Preserve D3D9 coordinate, depth, alpha-test, VPOS and sampler semantics.
- Add persistent shader/pipeline cache keys including GPU vendor/device/driver and shader/compiler version.
- Keep compilation off the critical draw path where possible; measure stalls before introducing asynchronous compilation.

A shader compiling successfully is not enough: link it with the correct vertex input and pipeline layout, then draw a known triangle before attempting BO1 scenes.

### D. Mali-G76 texture and format strategy

The recorded device reports:
- Vulkan API 1.1.177.
- RGBA8/BGRA8 supported.
- ASTC 4x4 supported.
- BC1 and BC3 format feature masks are zero.
- `textureCompressionBC=0`.

Therefore:
- Never require Vulkan BC compression on this device.
- Implement a verified CPU decode path for the DXT formats BO1 actually uses (including DXT1/DXT3/DXT5 and ATI/BC variants only when encountered).
- Upload decoded data to supported optimal-tiled Vulkan formats; track conversion/upload cost and cache decoded results.
- Query every format/usage combination needed for sampled images, color attachments and depth-stencil; do not infer renderability from sampled-image support.
- Add explicit tests for sRGB, mip levels, cube faces, volume slices, pitch, swizzle and alpha behavior.
- ASTC support is not a drop-in replacement for DXT: conversion/encoding and quality/cost must be measured before considering it.

### E. Performance and diagnostics

Add per-frame counters/timers for CPU submission, command recording, pipeline lookup/creation, shader compilation, texture decode/upload, buffer updates, queue waits and presentation. Log frame-time percentiles and GPU timestamps only when the feature is supported. Avoid `vkQueueWaitIdle` or device-wide idle in the normal frame loop.

The current WSI test intentionally waits idle after its single clear frame; that synchronization is diagnostic-only and must not be copied into the game frame loop.

## Milestones and acceptance gates

1. **V0 — platform WSI:** persistent instance/device/surface/swapchain lifecycle, clear every frame, resize/recreate, pause/resume, no GL context. No DXVK dependency in this test target.
2. **V1 — minimal GPU draw:** Vulkan triangle with vertex/index buffers, SPIR-V vertex/fragment shaders, descriptors and pipeline cache.
3. **V2 — D3D9 primitives:** route the D3D9 shim's basic device, state, shader, texture and draw calls to Vulkan; run focused conformance tests.
4. **V3 — BO1 front end:** render menus/UI, text, 2D assets and post-processing correctly.
5. **V4 — BO1 map:** render a simple map, then Kino der Toten with correct world geometry, textures, depth, blending and effects.
6. **V5 — performance:** profile Galaxy S10; optimize CPU submission, pipeline churn, DXT decode/upload, memory and synchronization based on measurements.

Do not declare the renderer game-capable before V4. A WSI clear, a triangle, or a menu alone does not establish full in-game compatibility.

## Build/package separation

Introduce a dedicated native-Vulkan build option/profile that:
- Does not set `BO1_NATIVE_DXVK`.
- Does not include DXVK headers or compile `bo1vk_dxvk.cpp` / the DXVK bridge.
- Does not run `build-dxvk-native-1.9.2.sh`.
- Does not package `libdxvk_d3d9.so` or require DXVK's license/source directory to package the APK.
- Links only Vulkan loader/platform requirements available from the Android NDK/device.
- Keeps the existing GLES renderer selectable as fallback during development.

Do not remove the working DXVK build profile until the independent Vulkan profile can build and run. The user's requested experimental APK must use the native Vulkan renderer explicitly; it must not silently fall back to DXVK.

## Immediate next engineering task

Implement V0 as a real persistent SDL/Vulkan video backend, then V1 as a triangle draw. Keep the existing diagnostic log and add lifecycle logs. Only after these gates pass should the D3D9 frontend be ported. Do not run GitHub Actions automatically; the repository owner runs the manual workflow.
