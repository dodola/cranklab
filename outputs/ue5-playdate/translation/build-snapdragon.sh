#!/usr/bin/env bash
# Cross-build libcrank-translate.so plus the ggml CPU/OpenCL/Hexagon plugins and HTP skeletons for
# Snapdragon Android, directly on Linux/WSL with the Android NDK and Hexagon/OpenCL SDKs.
# Optional: --docker uses the pinned toolchain image instead of locally installed SDKs.
# llama.cpp is cloned at the pinned revision and patched with patches/hymt2 (STQ1_0 + threadpool fix).
# Output: $OUT (default ../../../work/translation/snapdragon)/bin   -- idempotent, re-run is incremental.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
LOCK="$HERE/dependency-lock.json"
IMG="${SD_IMAGE:-ghcr.io/snapdragon-toolchain/arm64-android:v0.7}"
if [[ "${1:-}" == "--docker" ]]; then
  shift
  exec docker run --rm --platform linux/amd64 -u "$(id -u):$(id -g)" \
    -v "$(cd "$(dirname "$HERE")" && pwd):/project" \
    -v "${OUT:-$(cd "$HERE/../../.." && pwd)/work/translation/snapdragon}:/out" \
    -e OUT=/out -e JOBS="${JOBS:-$(nproc)}" \
    -w /project/translation "$IMG" bash build-snapdragon.sh "$@"
fi
[[ $# == 0 ]] || { echo "Usage: $0 [--docker]" >&2; exit 2; }
: "${ANDROID_NDK_ROOT:?Set ANDROID_NDK_ROOT to the Linux Android NDK directory}"
: "${HEXAGON_SDK_ROOT:?Set HEXAGON_SDK_ROOT to the Hexagon SDK directory}"
: "${HEXAGON_TOOLS_ROOT:?Set HEXAGON_TOOLS_ROOT to the Hexagon tools directory}"
# The pinned image installs OpenCL headers and its link library into the NDK sysroot.
OPENCL_SDK_ROOT="${OPENCL_SDK_ROOT:-$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr}"
[[ -f "$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" ]] || { echo "Android NDK toolchain not found" >&2; exit 1; }
[[ -d "$HEXAGON_SDK_ROOT" && -d "$HEXAGON_TOOLS_ROOT" && -d "$OPENCL_SDK_ROOT" ]] || { echo "Hexagon or OpenCL SDK not found" >&2; exit 1; }
OUT="${OUT:-$(cd "$HERE/../../.." && pwd)/work/translation/snapdragon}"
DEP="$HERE/deps/llama.cpp"
read -r REPO REV < <(python3 -c "import json;d=json.load(open('$LOCK'));print(d['llamaCppRepository'],d['llamaCppRevision'])")

mkdir -p "$OUT"
if [ ! -f "$DEP/CMakeLists.txt" ]; then git -c core.autocrlf=false clone --no-checkout "$REPO" "$DEP"; git -C "$DEP" -c core.autocrlf=false checkout -q "$REV"; fi
[ "$(git -C "$DEP" rev-parse HEAD)" = "$REV" ] || { echo "llama.cpp is not at the pinned $REV" >&2; exit 1; }

python3 - "$LOCK" "$HERE/patches/hymt2" "$DEP" "$OUT" <<'PY'
import hashlib,json,os,subprocess,sys,tempfile
lock=json.load(open(sys.argv[1]))
patches=[]
for name,sha in sorted(lock["hymt2PatchSha256"].items()):
    path=f"{sys.argv[2]}/{name}"
    if hashlib.sha256(open(path,"rb").read()).hexdigest()!=sha: sys.exit(f"patch checksum differs: {name}")
    patches.append(path)
# Later patches modify earlier ones, so testing each patch in reverse is not
# idempotent. Compare against an index containing the complete expected stack.
fd,index=tempfile.mkstemp(prefix="patch-index-",dir=sys.argv[4]);os.close(fd);os.unlink(index)
env=dict(os.environ,GIT_INDEX_FILE=index)
def git(*args,indexed=False,check=True):
    return subprocess.run(["git","-C",sys.argv[3],"-c","core.autocrlf=input","-c","core.filemode=false",*args],
        env=env if indexed else None,check=check)
try:
    git("read-tree",lock["llamaCppRevision"],indexed=True)
    for patch in patches:git("apply","--cached",patch,indexed=True)
    if git("diff","--quiet",indexed=True,check=False).returncode==0:
        print("Pinned patch stack already applied")
    else:
        if git("diff","--quiet","HEAD",check=False).returncode!=0:
            sys.exit("llama.cpp has changes outside the complete pinned patch stack; use a fresh dependency directory")
        for patch in patches:git("apply",patch)
        git("diff","--quiet",indexed=True)
        print("Pinned patch stack applied and verified")
finally:
    if os.path.exists(index):os.unlink(index)
PY
# Keep host code compatible with older ARM64 phones. ggml separately compiles
# CPU variants with newer instructions and selects them by runtime scoring.
cmake -S "$HERE" -B "$OUT/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static \
    "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
    "-DCMAKE_C_FLAGS=-march=armv8-a -ffp-model=fast -fno-finite-math-only -D_GNU_SOURCE" \
    "-DCMAKE_CXX_FLAGS=-march=armv8-a -ffp-model=fast -fno-finite-math-only -D_GNU_SOURCE" \
    "-DCMAKE_PREFIX_PATH=$OPENCL_SDK_ROOT" "-DHEXAGON_SDK_ROOT=$HEXAGON_SDK_ROOT" "-DHEXAGON_TOOLS_ROOT=$HEXAGON_TOOLS_ROOT" \
    -DPREBUILT_LIB_DIR=android_aarch64 -DCRANK_SNAPDRAGON=ON -DLLAMA_OPENSSL=OFF &&
  cmake --build "$OUT/build" -j"${JOBS:-$(nproc)}"
echo "built -> $OUT/build/bin"

# Stage for the Unreal plugin: CrankRuntime_APL.xml copies this folder into the APK's lib/arm64-v8a.
STAGE="${STAGE:-$(cd "$HERE/.." && pwd)/Unreal/Plugins/CrankRuntime/Binaries/ThirdParty/Translation/Android/arm64-v8a}"
mkdir -p "$STAGE"; rm -f "$STAGE"/*.so
cp "$OUT"/build/bin/*.so "$STAGE"/
find "$OUT/build/deps/llama.cpp/ggml/src/ggml-hexagon" -maxdepth 1 -name 'libggml-htp-v*.so' -exec cp {} "$STAGE"/ \;
echo "staged -> $STAGE ($(ls "$STAGE" | wc -l) libraries)"
