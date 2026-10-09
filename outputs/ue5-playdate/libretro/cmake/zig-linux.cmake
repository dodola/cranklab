# Cross toolchain using zig as the C/C++ compiler, so the glibc symbol version can be pinned.
# Usage: ZIG_CMD="python3 -m ziglang" ZIG_TARGET=aarch64-linux-gnu.2.17 \
#        cmake -DCMAKE_TOOLCHAIN_FILE=cmake/zig-linux.cmake -DZIG_ARCH=aarch64 ...
set(CMAKE_SYSTEM_NAME Linux)
if(NOT ZIG_ARCH)
    set(ZIG_ARCH aarch64)
endif()
set(CMAKE_SYSTEM_PROCESSOR ${ZIG_ARCH})
get_filename_component(_zig_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(CMAKE_C_COMPILER   "${_zig_dir}/zig-cc")
set(CMAKE_CXX_COMPILER "${_zig_dir}/zig-c++")
set(CMAKE_AR           "${_zig_dir}/zig-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB       "${_zig_dir}/zig-ranlib" CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
