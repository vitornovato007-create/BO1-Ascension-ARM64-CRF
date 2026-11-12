#!/usr/bin/env bash
# Build the patched Clang 21.1.8 required by the project's 32-bit-pointer builds.
# JOBS controls Ninja parallelism. The build directory is persisted by GitHub Actions cache.
set -Eeuo pipefail

L=/bo1/llvm
V=21.1.8
SRC="$L/llvm-project-$V.src"
BUILD="$L/build"
TARBALL="$L/llvm-project-$V.src.tar.xz"
mkdir -p "$L"
rm -f "$L/.bo1-build-complete"

cd "$L"
if [[ ! -f "$TARBALL" ]]; then
  echo "== Download LLVM $V source =="
  curl -fL --retry 3 --retry-delay 2 -o "$TARBALL" \
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-$V/llvm-project-$V.src.tar.xz"
fi

if [[ ! -f "$SRC/llvm/CMakeLists.txt" ]]; then
  echo "== Extract LLVM $V source =="
  rm -rf "$SRC"
  tar -xf "$TARBALL"
  for p in \
    /bo1/src/tools/p32/clang-21-x86_64-ptr32-abi.patch \
    /bo1/src/tools/p32/clang-21-aarch64-ptr32-truncstore.patch; do
    echo "== Apply $(basename "$p") =="
    patch -d "$SRC" -p1 < "$p"
  done
fi

echo "== Configure patched LLVM $V (Ninja jobs: ${JOBS:-4}) =="
cmake -S "$SRC/llvm" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS=clang \
  "-DLLVM_TARGETS_TO_BUILD=X86;AArch64" \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DCLANG_INCLUDE_TESTS=OFF \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DCMAKE_C_COMPILER=clang-21 \
  -DCMAKE_CXX_COMPILER=clang++-21 \
  -DLLVM_USE_LINKER=lld \
  -DLLVM_PARALLEL_LINK_JOBS=2 \
  -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF 2>&1 | tee "$L/configure.log"

echo "== Compile patched Clang (live Ninja progress; log: $L/build.log) =="
ninja -C "$BUILD" -j "${JOBS:-4}" clang clang-resource-headers 2>&1 | tee "$L/build.log"

"$BUILD/bin/clang" --version | head -n 1
touch "$L/.bo1-build-complete"
echo "== Patched LLVM build completed and is ready for cache =="
