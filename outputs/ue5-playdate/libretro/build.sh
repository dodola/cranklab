#!/bin/bash
# Build the CrankLab libretro core for Linux targets.
#   ./build.sh native                 host compiler (development)
#   ./build.sh x86_64|aarch64|armhf   zig cross build pinned to glibc 2.17 (handheld OS images)
# Zig is taken from $ZIG (default: "zig"), e.g. ZIG="python3 -m ziglang" after `pip install ziglang`.
# Output: dist/<target>/cranklab_libretro.so (stripped at link time)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
target="${1:-native}"
build="${BUILD_ROOT:-/tmp/cranklab-libretro}/$target"
dist="$here/dist/$target"
args=(-S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCRANKLAB_LUA_ONLY=${LUA_ONLY:-ON}")
case "$target" in
    native) ;;
    x86_64) arch=x86_64;  triple=x86_64-linux-gnu.2.17;      ;;
    aarch64) arch=aarch64; triple=aarch64-linux-gnu.2.17;    ;;
    armhf)  arch=arm;     triple=arm-linux-gnueabihf.2.17;  ;;
    *) echo "unknown target $target" >&2; exit 2 ;;
esac
if [ "$target" != native ]; then
    export ZIG_CMD="${ZIG:-zig}" ZIG_TARGET="$triple"
    args+=(-DCMAKE_TOOLCHAIN_FILE="$here/cmake/zig-linux.cmake" -DZIG_ARCH="$arch")
fi
cmake "${args[@]}"
cmake --build "$build" --parallel "${JOBS:-$(nproc)}"
mkdir -p "$dist"
cp "$build/cranklab_libretro.so" "$dist/"
cp "$build/cranklab_libretro.info" "$dist/"
# Optional CJK face for menu titles; copy the folder to <system dir>/cranklab/fonts/.
mkdir -p "$dist/system/cranklab/fonts"
cp "$here/../Unreal/Content/Games/launcher.pdx/fonts/cranklab-zh.pft" "$dist/system/cranklab/fonts/"
cp "$here/../translation/fonts/pocket-reader/PROVENANCE.txt" "$dist/system/cranklab/fonts/PROVENANCE.txt"
ls -l "$dist/cranklab_libretro.so"
