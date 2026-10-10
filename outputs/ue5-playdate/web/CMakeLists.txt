cmake_minimum_required(VERSION 3.20)
project(PlaydateWeb LANGUAGES C)
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Use emcmake or the Emscripten CMake toolchain")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(RT "${CMAKE_CURRENT_SOURCE_DIR}/../runtime")
set(DEPS "${RT}/deps")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/zlib" "${CMAKE_BINARY_DIR}/zlib" EXCLUDE_FROM_ALL)
# Emscripten maps both SHARED and STATIC libraries to .a; avoid zlib's two
# targets claiming the same libz.a in Ninja even though only static is linked.
set_target_properties(zlib PROPERTIES OUTPUT_NAME "z-web-unused")

# Keep the core's patched SDL mixer/dummy audio. WebGL and Web Audio belong to
# the browser host, not SDL. The dummy device produces PCM on its own pthread.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_PTHREADS ON CACHE BOOL "" FORCE)
foreach(feature JOYSTICK HAPTIC SENSOR POWER FILESYSTEM LOCALE MISC OPENGLES OPENGL CCACHE)
    set(SDL_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_VIDEO ON CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/sdl-source" "${CMAKE_BINARY_DIR}/sdl" EXCLUDE_FROM_ALL)
target_compile_definitions(SDL2-static PRIVATE CRANK_EMBEDDED_SDL)
foreach(feature SAMPLES VENDORED CMD MOD MIDI OPUS WAVPACK)
    set(SDL2MIXER_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${DEPS}/mixer-source" "${CMAKE_BINARY_DIR}/mixer" EXCLUDE_FROM_ALL)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/compat-include/SDL2")
file(WRITE "${CMAKE_BINARY_DIR}/compat-include/SDL2/SDL.h" "#include <SDL.h>\n")
file(WRITE "${CMAKE_BINARY_DIR}/compat-include/SDL2/SDL_mixer.h" "#include <SDL_mixer.h>\n")
file(GLOB LUA CONFIGURE_DEPENDS "${RT}/vendor/lua/*.c")
file(GLOB CORE CONFIGURE_DEPENDS "${RT}/src/*.c")
list(FILTER CORE EXCLUDE REGEX "pd_http_(win32|android)\\.c$")
add_executable(crank-web ${LUA} ${CORE} src/native_disabled.c)
target_compile_definitions(crank-web PRIVATE PD_LIBRARY LUA_32BITS SDL_MAIN_HANDLED CRANK_EMBEDDED_SDL _GNU_SOURCE)
target_compile_options(crank-web PRIVATE -include "${RT}/compat/pd_platform.h" -fno-strict-aliasing)
target_include_directories(crank-web PRIVATE "${RT}/include" "${RT}/src" "${RT}/vendor/lua"
    "${DEPS}/zlib" "${CMAKE_BINARY_DIR}/zlib" "${CMAKE_BINARY_DIR}/compat-include")
target_link_libraries(crank-web PRIVATE zlibstatic SDL2_mixer SDL2-static m)
set_target_properties(crank-web PROPERTIES OUTPUT_NAME "crank-core" SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/public/core")
target_link_options(crank-web PRIVATE
    --no-entry -pthread -O3
    "-sMODULARIZE=1" "-sEXPORT_ES6=1" "-sEXPORT_NAME=createCrankCore"
    "-sENVIRONMENT=web,worker" "-sPTHREAD_POOL_SIZE=4"
    "-sALLOW_MEMORY_GROWTH=1" "-sINITIAL_MEMORY=67108864" "-sMAXIMUM_MEMORY=536870912"
    "-sSTACK_SIZE=2097152" "-sDEFAULT_PTHREAD_STACK_SIZE=2097152"
    "-sEXIT_RUNTIME=0" "-sFORCE_FILESYSTEM=1" "-sFILESYSTEM=1" "-sFS_DEBUG=1"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString','FS','HEAPU8','HEAP16']"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_crank_start','_crank_stop','_crank_status','_crank_web_request_stop','_crank_web_can_join','_crank_set_buttons','_crank_add_crank','_crank_copy_frame','_crank_copy_audio','_crank_last_error','_crank_set_suspended','_crank_copy_menu','_crank_set_menu_open','_crank_menu_select','_crank_copy_menu_image']")
