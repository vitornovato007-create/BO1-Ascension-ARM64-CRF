# CRF DXVK Legacy Integration Audit

Status: investigation only. No build workflow was run and no runtime source/build script was modified.

Base: `main` at `3401a66355c07d6c590049c2f42b7635c9bf00ac`.

## Executive summary

A selectable DXVK backend is architecturally possible, but DXVK 1.10.3 cannot be dropped into this project as a prebuilt DLL or substituted by changing the version variable. The current port consumes **DXVK Native**, not the ordinary Wine/Proton DLL package. The old upstream 1.10.3 tree does not provide the same native D3D9 headers and SDL2 WSI integration expected by this port. Porting that native layer (or identifying a compatible maintained native fork) is the main engineering task.

Recommended next step: prototype a second **native ARM64 shared library** in an isolated source/build directory, preserving the current 2.6.1 build unchanged. Do not add a UI selector until both variants build, load, and pass a device-creation smoke test.

## Current integration traced in this repository

### 1. DXVK build

File: `tools/native/build-dxvk.sh`

- Pins upstream DXVK to `v2.6.1`.
- Clones into `/bo1/dxvk/src` and builds into target-specific Meson directories.
- Android target is `arm64-v8a`, using the NDK r30 toolchain and the SDL2 build produced by `build-android.sh`.
- Builds `src/d3d9/libdxvk_d3d9.so` with `-Ddxvk_native_wsi=sdl2`.
- Applies BO1-specific patches to SDL2 library naming, presenter handling of `VK_SUBOPTIMAL_KHR`, asynchronous graphics pipeline compilation (`BO1_DXVK_ASYNC`), and multisample resolve selection (`BO1_DXVK_RESOLVE`).
- The patching logic uses exact source snippets and assertions tied to the 2.6.1 source. Those patches must not be applied blindly to another branch/version.

### 2. Compile-time D3D9 interface

File: `cmake/native.cmake`

When `BO1_NATIVE_DXVK` is set, the build compiles `src/native/vk/bo1vk_dxvk.cpp` with `BO1_DXVK` and includes:

- `include/native/directx`
- `include/native/windows`

The native bridge therefore expects the DXVK Native header/API layout. This is not the same integration contract as a regular DXVK Wine DLL package.

### 3. Runtime loading

File: `src/native/vk/bo1vk_dxvk.cpp`

- Loads the native D3D9 shared library using `dlopen()`.
- Resolves `Direct3DCreate9` using `dlsym()`.
- Reads `BO1_DXVK_LIB`; defaults to `libdxvk_d3d9.so`.
- Caches the first availability/device check. The backend must be selected before the first call; changing it while the game process is running is not supported.
- The bridge can be compiled without DXVK, in which case Vulkan is unavailable and the GLES shim remains available.

This means a future selector should persist the selected library path/name before launching the native game process, not try to hot-swap a loaded library.

### 4. APK packaging

File: `tools/native/build-apk.sh`

- Currently packages one ARM64 library from `/bo1/dxvk/build-android-arm64-v8a/src/d3d9/libdxvk_d3d9.so`.
- It strips and renames it to `libdxvk_d3d9.so` under `lib/arm64-v8a/`.
- The x86_64 emulator path is also hard-coded to one DXVK output.
- The APK includes the upstream DXVK license from `/bo1/dxvk/src/LICENSE`.

A multi-backend package would need unique SONAMEs/file names, both license notices, explicit existence/ABI checks, and a runtime selection mechanism. Android's native library extraction/loading behavior must be verified for the chosen packaging layout.

## DXVK 1.10.3 feasibility

- Upstream DXVK 1.10.3 is a legacy Wine-oriented DXVK release. The current Ascension integration uses the later DXVK Native API and SDL2 WSI.
- DXVK Native's platform integration replaces Windows-specific window handles with platform objects and requires a WSI backend. The current build explicitly selects SDL2.
- Therefore, the 1.10.3 source would need a native D3D9 adaptation/backport: public native D3D9 interfaces, SDL2 WSI implementation, Android-compatible window/presentation handling, and any required build-system changes.
- The BO1-specific patches also need individual ports and tests. Some 2.6.1 changes may have no equivalent code location in 1.10.3.
- Vulkan 1.1 is the documented baseline for DXVK 1.10.3, versus Vulkan 1.3 and additional required extensions in modern DXVK. This is a compatibility reason to test 1.10.3, not a promise of higher FPS or correct rendering on Mali.

## DXVK-Sarek assessment

DXVK-Sarek is a promising *research source* because it maintains a 1.10.x-derived line and makes many Vulkan extensions optional for older hardware. Its documentation also warns that Mali devices may still show visual artifacts or stutter due to missing critical Vulkan features.

However, its documented distribution/build flow targets Wine/Proton DLLs. It is not automatically a native Android ARM64 `libdxvk_d3d9.so` compatible with Ascension. The same native D3D9/SDL2 WSI integration gap must be solved before it can be evaluated in this app.

Do not assume that the Sarek Windows DLLs can be renamed to `.so`, nor that a Box64/FEX/Winlator-compatible build is a native Android library.

## Recommended implementation sequence

1. Keep `v2.6.1` as the untouched known-good baseline.
2. Choose one candidate source: upstream `v1.10.3` or a specific DXVK-Sarek tag/commit. Pin the exact commit; do not follow a moving branch.
3. Build a compatibility spike outside the existing source/build directories:
   - confirm native D3D9 public headers and required COM interfaces;
   - add or backport SDL2 WSI for Android;
   - build ARM64 `libdxvk_d3d9_legacy.so`;
   - resolve SDL2 and C++ runtime dependencies;
   - run `readelf -d`/NDK checks and verify there are no unresolved symbols.
4. Port only the BO1 patches that have a proven counterpart in the candidate source. Add version-specific patch checks and fail closed if a patch does not match.
5. Add a small native smoke test that loads the library, resolves `Direct3DCreate9`, creates a D3D9 object/device, and presents a frame on Android.
6. Only after that succeeds, package both libraries and add a launcher preference that sets `BO1_DXVK_LIB` before native startup.
7. Compare Vulkan/DXVK versions on the same Galaxy S10 scene, map, resolution, thermal state, and test duration. Record average FPS, 1%/5% lows, frame time, device/driver, and logs.

## Files likely to change in the eventual implementation

- `tools/native/build-dxvk.sh` — version/commit-specific source and build directories, candidate native build, safe patch application.
- A new candidate-specific patch directory under `tools/native/patches/` — never mix 1.10.3 patches with 2.6.1 patches.
- `cmake/native.cmake` — only if the candidate native headers differ; keep the bridge API stable where possible.
- `tools/native/build-apk.sh` — package multiple uniquely named ARM64 libraries and licenses.
- Android launcher Java/configuration code — persist the selected backend and export `BO1_DXVK_LIB` before launching.
- `src/native/vk/bo1vk_dxvk.cpp` — likely no change needed if the candidate exports the same native D3D9 entry point and dependencies are compatible.

## Safety and validation

- No workflow was executed.
- No APK was built.
- No performance claim is made for DXVK 1.10.3 or Sarek on the Mali-G76.
- No current runtime source or build script was modified in this audit branch.
- Preserve the 2.6.1 baseline and test the candidate in isolation.


## Verificação adicional do tag upstream v1.10.3

A árvore Git do tag upstream `v1.10.3` foi consultada diretamente (tag object resolvido para commit `57740c6ac9c62f3f26c4b5237d7edf3c40797941`). A árvore contém os diretórios clássicos `src/d3d9`, `src/dxgi`, `src/dxvk` e os arquivos Meson, mas **não contém**:

- `include/native/directx` ou `include/native/windows`;
- `src/wsi/sdl2`;
- uma implementação Native WSI selecionável pelo build atual.

Isso confirma que o bloqueio não é apenas uma opção Meson com nome diferente: a interface Native usada pelo fork atual não existe nesse tag. O código atual de `bo1vk_dxvk.cpp` inclui `d3d9.h` do conjunto de headers Native e espera um `.so` exportando `Direct3DCreate9` com a janela SDL2 tratada pelo backend Native. A versão 1.10.3 upstream, sozinha, não satisfaz esse contrato.

### Decisão de engenharia para a próxima etapa

Não adicionar ainda um seletor de versão nem empacotar uma biblioteca fictícia. Um seletor só seria funcional depois de existir uma segunda biblioteca ARM64 real, com ABI Native D3D9 compatível e teste de criação/apresentação de dispositivo. A próxima implementação deve ser um port/backport isolado da camada Native/SDL2 para a base 1.10.3, com patches específicos e validação de dependências. O caminho 2.6.1 permanece intacto como fallback.

Esta verificação foi somente leitura: nenhum workflow foi executado e nenhum APK foi produzido.
