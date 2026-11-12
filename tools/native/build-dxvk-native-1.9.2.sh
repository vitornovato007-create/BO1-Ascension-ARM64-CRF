#!/bin/bash
# build-dxvk-native-1.9.2.sh [linux|arm64-v8a|x86_64]
# Builds the DXVK Native v1.9.2 ARM64 candidate separately from the existing DXVK 2.6.1 tree.
# Pinned source: q4a/dxvk-native @ dedfa1877afb9b9d30aaaa5935d56238ae12bb8b
# Output (Android ARM64): /bo1/dxvk-native-1.9.2/build-android-arm64-v8a/src/d3d9/libdxvk_d3d9.so
# For the engine build, pass DXVK=/bo1/dxvk-native-1.9.2/src to build-android.sh.
# For APK packaging, pass DXVK_DIR=/bo1/dxvk-native-1.9.2 to build-apk.sh.
set -euo pipefail

D=/bo1/dxvk-native-1.9.2
REPO=https://github.com/q4a/dxvk-native.git
COMMIT=dedfa1877afb9b9d30aaaa5935d56238ae12bb8b
TARGET=${1:-arm64-v8a}
NDK=/bo1/ndk/ndk-r30/android-ndk-r30
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
API=26
A=/bo1/android

if [ ! -d "$D/src/.git" ]; then
  mkdir -p "$D"
  git clone "$REPO" "$D/src" || exit 1
fi
git -C "$D/src" fetch --depth 1 origin "$COMMIT" || exit 1
git -C "$D/src" checkout --detach "$COMMIT" || exit 1

if [ "$TARGET" = linux ]; then
  B=$D/build-linux
  meson setup "$B" "$D/src" --buildtype release \
    -Denable_d3d9=true -Denable_dxgi=false -Denable_d3d10=false -Denable_d3d11=false \
    -Ddxvk_native_force=true -Ddxvk_native_wsi=sdl2
else
  if [ "$TARGET" = x86_64 ]; then
    TRIPLE=x86_64-linux-android$API; FAMILY=x86_64; SDLDIR=$A/sdl-x86_64
  else
    TARGET=arm64-v8a; TRIPLE=aarch64-linux-android$API; FAMILY=aarch64; SDLDIR=$A/sdl
  fi
  [ -f "$SDLDIR/lib/libSDL2.so" ] || { echo "missing $SDLDIR/lib/libSDL2.so; run build-android.sh first"; exit 1; }
  B=$D/build-android-$TARGET
  mkdir -p "$D/cross/$TARGET"
  PC=$D/cross/$TARGET
  cat > "$PC/sdl2.pc" <<EOF
Name: SDL2
Description: SDL2 for Android ($TARGET)
Version: 2.30.0
Cflags: -I$SDLDIR/include/SDL2 -I$SDLDIR/include
Libs: -L$SDLDIR/lib -lSDL2
EOF
  cp "$PC/sdl2.pc" "$PC/SDL2.pc"
  cat > "$PC/vulkan.pc" <<EOF
Name: Vulkan
Description: Android Vulkan loader
Version: 1.1
Cflags: -I$TC/sysroot/usr/include
Libs: -lvulkan
EOF
  cat > "$PC/cross.txt" <<EOF
[binaries]
c = '$TC/bin/$TRIPLE-clang'
cpp = '$TC/bin/$TRIPLE-clang++'
ar = '$TC/bin/llvm-ar'
strip = '$TC/bin/llvm-strip'
pkg-config = 'pkg-config'

[built-in options]
pkg_config_path = '$PC'
c_args = ['-fPIC']
cpp_args = ['-fPIC']
c_link_args = ['-Wl,-z,max-page-size=16384']
cpp_link_args = ['-static-libstdc++', '-Wl,-z,max-page-size=16384']

[properties]
pkg_config_libdir = '$PC'

[host_machine]
system = 'android'
cpu_family = '$FAMILY'
cpu = '$FAMILY'
endian = 'little'
EOF
  meson setup "$B" "$D/src" --cross-file "$PC/cross.txt" --buildtype release \
    -Denable_d3d9=true -Denable_dxgi=false -Denable_d3d10=false -Denable_d3d11=false \
    -Ddxvk_native_force=true -Ddxvk_native_wsi=sdl2
fi

nice -n 19 ionice -c3 ninja -C "$B" -j "${JOBS:-6}" -k 0 src/d3d9/libdxvk_d3d9.so
echo "DXVK Native 1.9.2 build completed: $B/src/d3d9/libdxvk_d3d9.so"
ls -lh "$B/src/d3d9/libdxvk_d3d9.so"
