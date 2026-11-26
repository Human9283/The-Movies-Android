cmake_minimum_required(VERSION 3.22.1)
project(the_movies_d3d9_probe C CXX)

if(NOT DEFINED DXVK_SOURCE_DIR OR NOT EXISTS "${DXVK_SOURCE_DIR}/include/native/directx/d3d9.h")
    message(FATAL_ERROR "DXVK_SOURCE_DIR must point to the pinned DXVK Native Android checkout")
endif()
if(NOT DEFINED SDL2_INCLUDE_DIR OR NOT EXISTS "${SDL2_INCLUDE_DIR}/SDL.h")
    message(FATAL_ERROR "SDL2_INCLUDE_DIR must point to SDL2/include")
endif()

set(SDL2_LIBRARY "${SDL2_LIB_DIR}/libSDL2.so")
if(NOT EXISTS "${SDL2_LIBRARY}")
    message(FATAL_ERROR "SDL2 shared library not found at ${SDL2_LIBRARY}")
endif()
find_library(ANDROID_LIBRARY android REQUIRED)
find_library(LOG_LIBRARY log REQUIRED)
find_library(DL_LIBRARY dl REQUIRED)
find_library(VULKAN_LIBRARY vulkan REQUIRED)

if(NOT DEFINED PCRECOMP_SOURCE_DIR OR NOT EXISTS "${PCRECOMP_SOURCE_DIR}/runtime/win32hle/win32hle.h")
    message(FATAL_ERROR "PCRECOMP_SOURCE_DIR must point to the pinned pcrecomp checkout")
endif()

# Compile the project's real D3D9 host bridge against this Android DXVK fork.
# This is an object-only ABI/header compatibility check; the probe APK does
# not link the game host or include lifted game code.
set(TM_D3D9_BACKEND_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/host/tm_d3d9_dxvk.cpp")
add_library(tm_d3d9_dxvk_compile OBJECT "${TM_D3D9_BACKEND_SOURCE}")
target_compile_features(tm_d3d9_dxvk_compile PRIVATE cxx_std_17)
target_compile_definitions(tm_d3d9_dxvk_compile PRIVATE _GNU_SOURCE RECOMP_RETADDR=0u NOMINMAX)
target_include_directories(tm_d3d9_dxvk_compile PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/host"
    "${PCRECOMP_SOURCE_DIR}/runtime/win32hle"
    "${PCRECOMP_SOURCE_DIR}/runtime/recomp32"
    "${SDL2_INCLUDE_DIR}"
    "${DXVK_SOURCE_DIR}/include/native/directx"
    "${DXVK_SOURCE_DIR}/include/native/windows"
    "${DXVK_SOURCE_DIR}/include/native/android"
    "${DXVK_SOURCE_DIR}/include/native"
)
target_compile_options(tm_d3d9_dxvk_compile PRIVATE -Wall -Wno-unused-function -Wno-unused-parameter)

# Compile the real portable game-host C sources with the Android NDK. This is
# deliberately object-only: the public repository does not contain the
# user's lifted Movies.exe C output, so this target validates Bionic/ARM64
# source compatibility without pretending to link or run the game.
get_filename_component(TM_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(TM_ANDROID_HOST_SOURCES
    "${TM_REPO_ROOT}/src/host/tm_host.c"
    "${TM_REPO_ROOT}/src/host/tm_host_android.c"
    "${TM_REPO_ROOT}/src/host/tm_shell.c"
    "${TM_REPO_ROOT}/src/host/tm_user32.c"
    "${TM_REPO_ROOT}/src/host/tm_imports.c"
    "${TM_REPO_ROOT}/src/host/tm_itrace.c"
    "${TM_REPO_ROOT}/src/host/tm_kernel32.c"
    "${TM_REPO_ROOT}/src/host/tm_except.c"
    "${TM_REPO_ROOT}/src/host/tm_loader.c"
    "${TM_REPO_ROOT}/src/host/tm_dinput8.c"
    "${TM_REPO_ROOT}/src/host/tm_sysroot.c"
    "${TM_REPO_ROOT}/src/host/tm_vfs.c"
    "${TM_REPO_ROOT}/src/host/tm_wmvcore.c"
    "${TM_REPO_ROOT}/src/host/tm_dshow.c"
    "${TM_REPO_ROOT}/src/host/tm_d3d9.c"
    "${TM_REPO_ROOT}/src/host/tm_ole32.c"
    "${TM_REPO_ROOT}/src/host/tm_winmm.c"
    "${TM_REPO_ROOT}/src/host/tm_trace.c"
    "${PCRECOMP_SOURCE_DIR}/runtime/win32hle/guest_mem.c"
    "${PCRECOMP_SOURCE_DIR}/runtime/win32hle/pe_loader.c"
    "${PCRECOMP_SOURCE_DIR}/runtime/win32hle/host.c"
)
add_library(tm_android_host_compile OBJECT ${TM_ANDROID_HOST_SOURCES})
target_compile_definitions(tm_android_host_compile PRIVATE
    _GNU_SOURCE RECOMP_RETADDR=0u TM_HAVE_DXVK=1)
target_include_directories(tm_android_host_compile PRIVATE
    "${TM_REPO_ROOT}/src/host"
    "${PCRECOMP_SOURCE_DIR}/runtime/win32hle"
    "${PCRECOMP_SOURCE_DIR}/runtime/recomp32"
    "${SDL2_INCLUDE_DIR}"
)
target_compile_options(tm_android_host_compile PRIVATE
    -Wall -Wno-unused-function -Wno-unused-parameter -fno-strict-aliasing)


add_library(vulkanpreflight SHARED vulkan_preflight.c)
target_link_libraries(vulkanpreflight PRIVATE ${ANDROID_LIBRARY} ${LOG_LIBRARY} ${VULKAN_LIBRARY})
target_compile_options(vulkanpreflight PRIVATE -Wall -Wextra -Werror=return-type)

add_library(main SHARED main.cpp)
add_dependencies(main tm_d3d9_dxvk_compile tm_android_host_compile)
target_compile_features(main PRIVATE cxx_std_17)
target_compile_definitions(main PRIVATE SDL_MAIN_HANDLED NOMINMAX)
target_include_directories(main PRIVATE
    "${SDL2_INCLUDE_DIR}"
    "${DXVK_SOURCE_DIR}/include/native/directx"
    "${DXVK_SOURCE_DIR}/include/native/windows"
    "${DXVK_SOURCE_DIR}/include/native"
)
target_compile_options(main PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(main PRIVATE ${SDL2_LIBRARY} ${ANDROID_LIBRARY} ${LOG_LIBRARY} ${DL_LIBRARY} ${VULKAN_LIBRARY})
