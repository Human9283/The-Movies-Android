# Dedicated Android target. The desktop CMakeLists.txt remains untouched.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(TM_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PCRECOMP_SOURCE_DIR "" CACHE PATH "Pinned pcrecomp checkout")
set(TM_GEN "" CACHE PATH "Private output directory from run_lift.py")
set(SDL2_ROOT "" CACHE PATH "SDL2 source root (headers)")
set(SDL2_LIBRARY "" CACHE FILEPATH "Built Android libSDL2.so")
set(SDL2_TTF_INCLUDE_DIR "" CACHE PATH "SDL2_ttf compatibility include root containing SDL2/SDL_ttf.h")
set(SDL2_TTF_LIBRARY "" CACHE FILEPATH "Built Android libSDL2_ttf.so")
set(DXVK_ROOT "" CACHE PATH "Android DXVK Native source root (headers)")
set(DXVK_D3D9_LIBRARY "" CACHE FILEPATH "Built Android libdxvk_d3d9.so")

foreach(required PCRECOMP_SOURCE_DIR TM_GEN SDL2_ROOT SDL2_LIBRARY SDL2_TTF_INCLUDE_DIR SDL2_TTF_LIBRARY DXVK_ROOT DXVK_D3D9_LIBRARY)
  if("${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} must be set")
  endif()
endforeach()
foreach(check
    "${PCRECOMP_SOURCE_DIR}/runtime/win32hle/win32hle.h"
    "${TM_GEN}/recomp_dispatch.S"
    "${SDL2_ROOT}/include/SDL.h"
    "${SDL2_LIBRARY}"
    "${SDL2_TTF_INCLUDE_DIR}/SDL2/SDL_ttf.h"
    "${SDL2_TTF_LIBRARY}"
    "${DXVK_ROOT}/include/native/directx/d3d9.h"
    "${DXVK_D3D9_LIBRARY}")
  if(NOT EXISTS "${check}")
    message(FATAL_ERROR "Required Android game-build input missing: ${check}")
  endif()
endforeach()

if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "This target is for Android arm64-v8a only")
endif()

find_program(PATCH_EXE patch REQUIRED)
set(HLE_PATCHED "${CMAKE_BINARY_DIR}/win32hle_patched")
file(MAKE_DIRECTORY "${HLE_PATCHED}")
foreach(pf hle_path.c hle_path_selftest.c screen.c)
  if(EXISTS "${PCRECOMP_SOURCE_DIR}/runtime/win32hle/${pf}")
    configure_file("${PCRECOMP_SOURCE_DIR}/runtime/win32hle/${pf}" "${HLE_PATCHED}/${pf}" COPYONLY)
  endif()
endforeach()
file(GLOB TM_HLE_PATCHES "${TM_ROOT}/patches/pcrecomp/*.patch")
list(SORT TM_HLE_PATCHES)
foreach(pp IN LISTS TM_HLE_PATCHES)
  execute_process(COMMAND "${PATCH_EXE}" -s -p3 -d "${HLE_PATCHED}" -i "${pp}"
                  RESULT_VARIABLE patch_result)
  if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "Could not apply ${pp}; pcrecomp revision mismatch")
  endif()
endforeach()

file(GLOB HLE_SOURCES CONFIGURE_DEPENDS "${PCRECOMP_SOURCE_DIR}/runtime/win32hle/*.c")
list(FILTER HLE_SOURCES EXCLUDE REGEX "_selftest\\.c$")
list(FILTER HLE_SOURCES EXCLUDE REGEX "/(hle_path|screen)\\.c$")
list(APPEND HLE_SOURCES "${HLE_PATCHED}/hle_path.c" "${HLE_PATCHED}/screen.c")

file(GLOB TM_GEN_C_SOURCES CONFIGURE_DEPENDS "${TM_GEN}/recomp_*.c")
list(FILTER TM_GEN_C_SOURCES EXCLUDE REGEX "/recomp_dispatch\\.c$")
set(TM_GEN_ASM "${TM_GEN}/recomp_dispatch.S")
if(NOT TM_GEN_C_SOURCES OR NOT EXISTS "${TM_GEN_ASM}")
  message(FATAL_ERROR "Expected lifted C files and generated ARM64 recomp_dispatch.S in TM_GEN")
endif()
set(TM_GEN_SOURCES ${TM_GEN_C_SOURCES} "${TM_GEN_ASM}")

set(TM_RUNTIME_SOURCES
    "${TM_ROOT}/src/host/tm_host.c"
    "${TM_ROOT}/src/host/tm_host_android.c"
    "${TM_ROOT}/src/host/tm_shell.c"
    "${TM_ROOT}/src/host/tm_user32.c"
    "${TM_ROOT}/src/host/tm_imports.c"
    "${TM_ROOT}/src/host/tm_itrace.c"
    "${TM_ROOT}/src/host/tm_kernel32.c"
    "${TM_ROOT}/src/host/tm_except.c"
    "${TM_ROOT}/src/host/tm_loader.c"
    "${TM_ROOT}/src/host/tm_dinput8.c"
    "${TM_ROOT}/src/host/tm_sysroot.c"
    "${TM_ROOT}/src/host/tm_vfs.c"
    "${TM_ROOT}/src/host/tm_wmvcore.c"
    "${TM_ROOT}/src/host/tm_dshow.c"
    "${TM_ROOT}/src/host/tm_d3d9.c"
    "${TM_ROOT}/src/host/tm_ole32.c"
    "${TM_ROOT}/src/host/tm_winmm.c"
    "${TM_ROOT}/src/host/tm_trace.c"
    "${TM_ROOT}/src/host/tm_dispatch_layout_check.c"
    ${HLE_SOURCES}
    ${TM_GEN_SOURCES}
)

add_library(SDL2 SHARED IMPORTED GLOBAL)
set_target_properties(SDL2 PROPERTIES IMPORTED_LOCATION "${SDL2_LIBRARY}")
add_library(SDL2_TTF SHARED IMPORTED GLOBAL)
set_target_properties(SDL2_TTF PROPERTIES IMPORTED_LOCATION "${SDL2_TTF_LIBRARY}")
add_library(DXVK_D3D9 SHARED IMPORTED GLOBAL)
set_target_properties(DXVK_D3D9 PROPERTIES IMPORTED_LOCATION "${DXVK_D3D9_LIBRARY}")

# The lifted translation units are unusually large. Limit Ninja compile concurrency
# to one TU to keep peak RAM predictable on GitHub-hosted runners.
set_property(GLOBAL PROPERTY JOB_POOLS tm_compile_pool=1)

add_library(main SHARED ${TM_RUNTIME_SOURCES} "${TM_ROOT}/src/host/tm_d3d9_dxvk.cpp")
target_compile_features(main PRIVATE c_std_11 cxx_std_17)
target_compile_definitions(main PRIVATE _GNU_SOURCE RECOMP_RETADDR=0u RECOMP_TRACE=1
    TM_HAVE_DXVK=1 SDL_MAIN_HANDLED NOMINMAX)
target_include_directories(main PRIVATE
    "${TM_ROOT}/src/host"
    "${TM_GEN}"
    "${PCRECOMP_SOURCE_DIR}/runtime/recomp32"
    "${PCRECOMP_SOURCE_DIR}/runtime/win32hle"
    "${SDL2_ROOT}/include"
    "${SDL2_TTF_INCLUDE_DIR}"
    "${DXVK_ROOT}/include/native/directx"
    "${DXVK_ROOT}/include/native/windows"
    "${DXVK_ROOT}/include/native/android"
    "${DXVK_ROOT}/include/native")
set_source_files_properties(${TM_GEN_C_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS RECOMP_LOCAL_REGS
    COMPILE_OPTIONS "-g0")
target_compile_options(main PRIVATE -O1 -fno-strict-aliasing -fwrapv -Wno-unused-function -Wno-unused-parameter)
target_link_libraries(main PRIVATE SDL2_TTF SDL2 DXVK_D3D9 android log dl vulkan m)
set_target_properties(main PROPERTIES
    OUTPUT_NAME main
    JOB_POOL_COMPILE tm_compile_pool
)

# The generated translation units are large; keep their optimization modest
# for the first ARM64 link. Raise only after the initial APK is reproducible.
