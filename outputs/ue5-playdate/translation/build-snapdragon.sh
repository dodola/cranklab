#!/usr/bin/env bash
# Cross-build libcrank-translate.so plus the ggml CPU/OpenCL/Hexagon plugins and HTP skeletons for
# Snapdragon Android, inside Qualcomm's toolchain image (NDK, OpenCL SDK and Hexagon SDK bundled).
# llama.cpp is cloned at the pinned revision and patched with patches/hymt2 (STQ1_0 + threadpool fix).
# Output: $OUT (default ../../../work/translation/snapdragon)/bin   -- idempotent, re-run is incremental.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
LOCK="$HERE/dependency-lock.json"
IMG="${SD_IMAGE:-ghcr.io/snapdragon-toolchain/arm64-android:v0.7}"
OUT="${OUT:-$(cd "$HERE/../../.." && pwd)/work/translation/snapdragon}"
DEP="$HERE/deps/llama.cpp"
read -r REPO REV < <(python3 -c "import json;d=json.load(open('$LOCK'));print(d['llamaCppRepository'],d['llamaCppRevision'])")

if [ ! -f "$DEP/CMakeLists.txt" ]; then git clone --no-checkout "$REPO" "$DEP"; git -C "$DEP" checkout -q "$REV"; fi
[ "$(git -C "$DEP" rev-parse HEAD)" = "$REV" ] || { echo "llama.cpp is not at the pinned $REV" >&2; exit 1; }

python3 - "$LOCK" "$HERE/patches/hymt2" <<'PY' | while read -r file; do
import hashlib,json,sys
lock=json.load(open(sys.argv[1]))
for name,sha in sorted(lock["hymt2PatchSha256"].items()):
    path=f"{sys.argv[2]}/{name}"
    if hashlib.sha256(open(path,"rb").read()).hexdigest()!=sha: sys.exit(f"patch checksum differs: {name}")
    print(path)
PY
  if git -C "$DEP" apply --reverse --check "$file" 2>/dev/null; then continue; fi
  git -C "$DEP" apply --check "$file" && git -C "$DEP" apply "$file"
done

cp -u "$DEP/docs/backend/snapdragon/CMakeUserPresets.json" "$DEP/CMakeUserPresets.json"
mkdir -p "$OUT"
docker run --rm --platform linux/amd64 -u "$(id -u):$(id -g)" \
  -v "$HERE:/workspace" -v "$OUT:/out" -w /workspace "$IMG" bash -c "
  # The preset supplies toolchain, -march=armv8.7a+i8mm and SDK paths; it lives in the llama.cpp root.
  cmake -S /workspace -B /out/build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-34 -DANDROID_STL=c++_static \
    -DCMAKE_TOOLCHAIN_FILE=\$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
    \"-DCMAKE_C_FLAGS=-march=armv8.7a+fp16+dotprod+i8mm -ffp-model=fast -fno-finite-math-only -D_GNU_SOURCE\" \
    \"-DCMAKE_CXX_FLAGS=-march=armv8.7a+fp16+dotprod+i8mm -ffp-model=fast -fno-finite-math-only -D_GNU_SOURCE\" \
    -DCMAKE_PREFIX_PATH=\$OPENCL_SDK_ROOT -DHEXAGON_SDK_ROOT=\$HEXAGON_SDK_ROOT -DHEXAGON_TOOLS_ROOT=\$HEXAGON_TOOLS_ROOT \
    -DPREBUILT_LIB_DIR=android_aarch64 -DCRANK_SNAPDRAGON=ON -DLLAMA_OPENSSL=OFF &&
  cmake --build /out/build -j${JOBS:-$(nproc)}"
echo "built -> $OUT/build/bin"
