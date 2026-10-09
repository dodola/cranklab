#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
target="${1:-switch}"
task_out="${OUT:-$task_root/build-$target}"
if [[ "$target" == switch ]]; then
    export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
    python3 "$task_root/tools/prepare.py"
    cmake -S "$task_root" -B "$task_out" -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" -DCMAKE_BUILD_TYPE=Release
elif [[ "$target" == host ]]; then
    cmake -S "$task_root" -B "$task_out" -DCMAKE_BUILD_TYPE=RelWithDebInfo
else
    echo 'Usage: build.sh switch|host' >&2
    exit 2
fi
cmake --build "$task_out" -j "${JOBS:-8}"
if [[ "$target" == host ]]; then ctest --test-dir "$task_out" --output-on-failure; fi
