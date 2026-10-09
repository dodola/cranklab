"""Prepare an isolated copy of the production bridge; never modify production binaries.

OpenCL is dispatched through Android's public Pixel wrapper when available. The
vendor driver is not copied into the APK. Other devices use public libOpenCL.so.
"""
import argparse, hashlib, json, re
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('--cache',default='D:/PlaydateAI/hymt2-gpu-experiment')
p.add_argument('--backend',choices=['opencl','vulkan'],default='opencl');p.add_argument('--generic',action='store_true');a=p.parse_args()
root=Path(a.cache).resolve();assert root.drive.upper()!='C:', 'Experimental caches belong on D drive'
project=Path(__file__).resolve().parents[1];llama=project/'translation/deps/llama.cpp'
source=root/('source-'+a.backend+('-generic' if a.generic else ''));source.mkdir(parents=True,exist_ok=True)
bridge=(project/'translation/crank_translate.cpp').read_text(encoding='utf-8')
bridge=bridge.replace('#include "deps/llama.cpp/vendor/hash/sha256/sha256.h"','#include "sha256/sha256.h"')
assert bridge.count('mp.n_gpu_layers=0;')==1
bridge=bridge.replace('mp.n_gpu_layers=0;', 'mp.n_gpu_layers=std::atoi(std::getenv("CRANK_PROBE_GPU_LAYERS")?std::getenv("CRANK_PROBE_GPU_LAYERS"):"0"); if(mp.n_gpu_layers>0 && !llama_supports_gpu_offload())return fail(error,error_capacity,"gpu_unavailable");')
assert 'static void quiet_log(ggml_log_level,const char*,void*) {}' in bridge
bridge=bridge.replace('static void quiet_log(ggml_log_level,const char*,void*) {}','static void quiet_log(ggml_log_level,const char* text,void*) {__android_log_write(ANDROID_LOG_INFO,"HyMtBackend",text);}')
bridge='#include <android/log.h>\n#include <cstdlib>\n'+bridge
(source/'crank_translate.cpp').write_text(bridge,encoding='utf-8')
jni=(project/'tools/android-translation-speed/speedprobe.cpp').read_text(encoding='utf-8')
jni=jni.replace('Java_com_cranklab_SpeedProbe_runNative','Java_com_cranklab_hymtgpu_GpuProbe_runNative')
jni=jni.replace('jbyteArray input,jint repeats)', 'jbyteArray input,jint repeats,jint layers,jstring cacheValue)')
jni=jni.replace('    auto model=text(env,modelValue)', '    std::string gpuLayers=std::to_string(layers);setenv("CRANK_PROBE_GPU_LAYERS",gpuLayers.c_str(),1);auto cache=text(env,cacheValue);setenv("GGML_OPENCL_KERNEL_CACHE_DIR",cache.c_str(),1);\n    auto model=text(env,modelValue)')
jni=jni.replace('if(!library)return bytes(env,"[]");','if(!library)return bytes(env,"[{\\"code\\":1,\\"error\\":"+jsonString(dlerror())+"}]");')
jni='#include <cstdlib>\n'+jni
(source/'speedprobe.cpp').write_text(jni,encoding='utf-8')

cmake='''cmake_minimum_required(VERSION 3.22)
project(hymt_gpu_probe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_CPU_ARM_ARCH "armv8.2-a+dotprod+fp16" CACHE STRING "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_LLAMAFILE OFF CACHE BOOL "" FORCE)
foreach(option LLAMA_BUILD_COMMON LLAMA_BUILD_TESTS LLAMA_BUILD_EXAMPLES LLAMA_BUILD_TOOLS LLAMA_CURL)
  set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
add_link_options("-Wl,-z,max-page-size=16384")
'''
cmake+=f'set(GGML_{a.backend.upper()} ON CACHE BOOL "" FORCE)\n'
cmake+=f'add_subdirectory("{llama.as_posix()}" llama)\n'
if a.backend=='opencl':
    # Force-include after cl.h: call sites use cached, correctly typed pointers.
    # This avoids a forbidden DT_NEEDED on Pixel's private libOpenCL.so.
    calls=set()
    for file in ('ggml-opencl.cpp','cl-program-cache.cpp'):
        calls.update(re.findall(r'\b(cl[A-Z]\w+)\s*\(', (llama/'ggml/src/ggml-opencl'/file).read_text(encoding='utf-8')))
    header='''#pragma once
#define CL_TARGET_OPENCL_VERSION 300
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cstdio>
inline void* probe_cl_symbol(const char* name) {
    struct Loader {
        void* handle; void*(*pixel)(const char*)=nullptr;
        Loader() {
            handle=dlopen("libOpenCL-pixel.so",RTLD_NOW|RTLD_LOCAL);
            if(handle) {
                auto enable=reinterpret_cast<void*(*)()>(dlsym(handle,"enableOpenCL"));
                pixel=reinterpret_cast<void*(*)(const char*)>(dlsym(handle,"loadOpenCLPointer"));
                if(!enable||!pixel||!enable())pixel=nullptr;
            }
            if(!pixel)handle=dlopen("libOpenCL.so",RTLD_NOW|RTLD_LOCAL);
        }
    };
    static Loader loader;
    void* result=loader.pixel?loader.pixel(name):(loader.handle?dlsym(loader.handle,name):nullptr);
    if(!result){fprintf(stderr,"HY GPU missing OpenCL symbol: %s\\n",name);abort();}
    return result;
}
'''
    for call in sorted(calls):
        header+=f'#define {call} ([]() {{static auto f=reinterpret_cast<decltype(&{call})>(probe_cl_symbol("{call}"));return f;}}())\n'
    (source/'pixel-opencl.h').write_text(header,encoding='utf-8')
    cmake+=f'target_compile_options(ggml-opencl PRIVATE -include "{(source/"pixel-opencl.h").as_posix()}")\n'
cmake+='''add_library(crank-translate SHARED crank_translate.cpp)
target_include_directories(crank-translate PRIVATE "'''+(llama/'vendor/hash').as_posix()+'''")
target_link_libraries(crank-translate PRIVATE llama vendor-hash log dl)
add_library(speedprobe SHARED speedprobe.cpp)
target_link_libraries(speedprobe PRIVATE log dl)
'''
(source/'CMakeLists.txt').write_text(cmake,encoding='utf-8')
(source/'preparation.json').write_text(json.dumps({'bridgeSourceSha256':hashlib.sha256((project/'translation/crank_translate.cpp').read_bytes()).hexdigest(),'backend':a.backend,'generic':a.generic,'dispatchSymbols':sorted(calls) if a.backend=='opencl' else []},indent=2))
print(source)
