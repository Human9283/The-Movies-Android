# =============================================================================
# The Movies (Movies.exe 1.1, Russian CD) on pcrecomp's win32hle.
#
# 64-bit host, guest memory "based" (the guest's 4 GB in one mapping).
# The active target graph below is currently a Linux desktop build only.
# Linux AArch64 and Android arm64-v8a remain porting targets, not verified
# build configurations; see docs/ANDROID-PORT-AUDIT.md.
#
#   cmake -S . -B build -G Ninja -DPCRECOMP=/path/to/pcrecomp -DTM_GEN=/path/to/gen
#   cmake --build build
#
# TM_GEN is the lifted C from run_lift.py.
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(themovies_android C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "64-bit hosts only (based guest memory)")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(PCRECOMP "" CACHE PATH "pcrecomp checkout (runtime/win32hle, runtime/recomp32)")
set(TM_GEN   "" CACHE PATH "lifted C from run_lift.py (recomp_*.c, recomp_dispatch.c, recomp_funcs.h)")
if(NOT EXISTS "${PCRECOMP}/runtime/win32hle/win32hle.h")
  message(FATAL_ERROR "set -DPCRECOMP to a pcrecomp checkout")
endif()
file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${TM_GEN}/*.c")
if(NOT GEN_SOURCES)
  message(FATAL_ERROR "no lifted C in TM_GEN='${TM_GEN}' -- run run_lift.py first (docs/BUILD.md)")
endif()

# Fixed guest base: one constant add per access instead of a reloaded global.
# Empty = the base is a variable (g_mem_base) chosen at run time.
set(TM_MEM_BASE "" CACHE STRING "fixed guest memory base, e.g. 0x100000000000ull (empty: variable)")
option(TM_LOCAL_REGS "lifted bodies keep the guest registers in locals" ON)
option(TM_TRACE "RECOMP_ENTER hook (needed for the [poc] WinMain marker)" ON)

file(GLOB HLE_SOURCES CONFIGURE_DEPENDS "${PCRECOMP}/runtime/win32hle/*.c")
list(FILTER HLE_SOURCES EXCLUDE REGEX "_selftest\\.c$")

# Patches to win32hle (patches/pcrecomp/*.patch), applied to copies in the
# build tree: the pcrecomp checkout itself is never changed. A patch that
# does not apply stops the configure step (a different pcrecomp revision).
#   0001: guest directory mounts in hle_path.c (C:\WINDOWS -> the runtime
#         system directory), and guest paths made absolute the Windows way
#         before they are matched (docs/IMPORTS.md, "The virtual file system").
#   0002: win32hle's path selftest, for 0001's relative-name change.
#   0003: screen.c holds the mouse in a Vulkan or GL window as in an SDL
#         renderer's, so an exclusive DirectInput mouse gets relative
#         movement there too (docs/DXVK-INTEGRATION.md 5).
set(HLE_PATCHED "${CMAKE_BINARY_DIR}/win32hle_patched")
file(MAKE_DIRECTORY "${HLE_PATCHED}")
find_program(PATCH_EXE patch REQUIRED)
foreach(pf hle_path.c hle_path_selftest.c screen.c)
  configure_file("${PCRECOMP}/runtime/win32hle/${pf}" "${HLE_PATCHED}/${pf}" COPYONLY)
endforeach()
file(GLOB TM_HLE_PATCHES "${CMAKE_SOURCE_DIR}/patches/pcrecomp/*.patch")
list(SORT TM_HLE_PATCHES)
foreach(pp ${TM_HLE_PATCHES})
  execute_process(COMMAND ${PATCH_EXE} -s -p3 -d "${HLE_PATCHED}" -i "${pp}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "patch ${pp} does not apply to ${PCRECOMP} (another pcrecomp revision?)")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TM_HLE_PATCHES})
list(FILTER HLE_SOURCES EXCLUDE REGEX "/(hle_path|screen)\\.c$")
list(APPEND HLE_SOURCES "${HLE_PATCHED}/hle_path.c" "${HLE_PATCHED}/screen.c")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2 SDL2_ttf)
find_package(Threads REQUIRED)

add_library(tm_gen OBJECT ${GEN_SOURCES})
add_executable(tm_host src/host/tm_host.c src/host/tm_shell.c src/host/tm_user32.c src/host/tm_imports.c src/host/tm_itrace.c src/host/tm_kernel32.c src/host/tm_except.c src/host/tm_loader.c src/host/tm_dinput8.c src/host/tm_sysroot.c src/host/tm_vfs.c src/host/tm_wmvcore.c src/host/tm_dshow.c src/host/tm_d3d9.c src/host/tm_ole32.c src/host/tm_winmm.c ${HLE_SOURCES}
               src/host/tm_trace.c $<TARGET_OBJECTS:tm_gen>)

foreach(t tm_gen tm_host)
  target_include_directories(${t} PRIVATE ${CMAKE_SOURCE_DIR}/src/host ${TM_GEN} ${PCRECOMP}/runtime/recomp32 ${PCRECOMP}/runtime/win32hle)
  target_compile_definitions(${t} PRIVATE _GNU_SOURCE RECOMP_RETADDR=0u)
  if(TM_MEM_BASE)
    target_compile_definitions(${t} PRIVATE RECOMP_MEM_BASE=${TM_MEM_BASE})
  endif()
  if(TM_TRACE)
    target_compile_definitions(${t} PRIVATE RECOMP_TRACE=1)
  endif()
endforeach()
if(TM_LOCAL_REGS)
  target_compile_definitions(tm_gen PRIVATE RECOMP_LOCAL_REGS)
endif()
# The generated C: warnings off (millions of them), -O1 by default to keep the
# compile within memory on a 4-core build machine; raise for speed tests.
set(TM_GEN_OPT "-O1" CACHE STRING "optimisation for the generated C")
target_compile_options(tm_gen PRIVATE -w ${TM_GEN_OPT} -fno-strict-aliasing -fwrapv)
target_compile_options(tm_host PRIVATE -Wall -Wno-unused-function -fno-strict-aliasing)
target_link_libraries(tm_host PRIVATE PkgConfig::SDL2 Threads::Threads m)

# Tests of our own shims (tests/c): each links its shim files with win32hle,
# without the lifted code. ctest --test-dir build
enable_testing()
function(tm_c_test name)
  add_executable(${name} tests/c/${name}.c ${ARGN} ${HLE_SOURCES})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src/host ${PCRECOMP}/runtime/recomp32 ${PCRECOMP}/runtime/win32hle)
  target_compile_definitions(${name} PRIVATE _GNU_SOURCE RECOMP_RETADDR=0u)
  target_compile_options(${name} PRIVATE -Wall -Wno-unused-function -fno-strict-aliasing)
  target_link_libraries(${name} PRIVATE PkgConfig::SDL2 Threads::Threads m)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endfunction()
tm_c_test(test_disk_free_w src/host/tm_kernel32.c)
tm_c_test(test_loader src/host/tm_loader.c src/host/tm_kernel32.c)
tm_c_test(test_dinput8 src/host/tm_dinput8.c src/host/tm_loader.c)
tm_c_test(test_sysroot src/host/tm_sysroot.c src/host/tm_vfs.c src/host/tm_loader.c src/host/tm_kernel32.c)
tm_c_test(test_vfs src/host/tm_vfs.c src/host/tm_sysroot.c src/host/tm_loader.c src/host/tm_wmvcore.c src/host/tm_dinput8.c)
tm_c_test(test_vfs_audit src/host/tm_vfs.c src/host/tm_sysroot.c src/host/tm_loader.c)
tm_c_test(test_loader_audit src/host/tm_loader.c src/host/tm_dinput8.c)
tm_c_test(test_dshow src/host/tm_dshow.c)
tm_c_test(test_d3d9 src/host/tm_d3d9.c src/host/tm_loader.c)
tm_c_test(test_ole32 src/host/tm_ole32.c)
tm_c_test(test_winmm src/host/tm_winmm.c)
# win32hle's own path selftest (patched by 0002), run against the patched hle_path.c
add_executable(hle_path_selftest ${HLE_PATCHED}/hle_path_selftest.c ${HLE_SOURCES})
target_include_directories(hle_path_selftest PRIVATE ${PCRECOMP}/runtime/recomp32 ${PCRECOMP}/runtime/win32hle)
target_compile_definitions(hle_path_selftest PRIVATE _GNU_SOURCE RECOMP_RETADDR=0u)
target_compile_options(hle_path_selftest PRIVATE -w -fno-strict-aliasing)
target_link_libraries(hle_path_selftest PRIVATE PkgConfig::SDL2 Threads::Threads m)
add_test(NAME hle_path_selftest COMMAND hle_path_selftest)

# ---- DXVK Native (docs/DXVK-INTEGRATION.md) ----------------------------------
# TM_DXVK_PREFIX: an install of DXVK Native's d3d9 (meson install, SDL2 WSI).
# With it:
# - dxvk_present is built: a real D3D9 device on Vulkan, cleared, presented
#   and read back. Its test needs xvfb-run and a Vulkan 1.3 driver (Mesa's
#   lavapipe will do);
# - tm_host gets the DXVK device backend (src/host/tm_d3d9_dxvk.cpp), used
#   with --d3d9 dxvk.
set(TM_DXVK_PREFIX "" CACHE PATH "DXVK Native install prefix (include/dxvk, lib*/libdxvk_d3d9.so)")
if(TM_DXVK_PREFIX)
  enable_language(CXX)
  find_library(DXVK_D3D9 dxvk_d3d9 PATHS "${TM_DXVK_PREFIX}/lib" "${TM_DXVK_PREFIX}/lib/x86_64-linux-gnu"
               "${TM_DXVK_PREFIX}/lib/aarch64-linux-gnu" NO_DEFAULT_PATH REQUIRED)
  add_executable(dxvk_present tests/dxvk/dxvk_present.cpp)
  set_target_properties(dxvk_present PROPERTIES CXX_STANDARD 17)
  target_include_directories(dxvk_present PRIVATE "${TM_DXVK_PREFIX}/include/dxvk")
  target_link_libraries(dxvk_present PRIVATE ${DXVK_D3D9} PkgConfig::SDL2)
  get_filename_component(DXVK_LIBDIR "${DXVK_D3D9}" DIRECTORY)
  set_target_properties(dxvk_present PROPERTIES BUILD_RPATH "${DXVK_LIBDIR}")
  target_sources(tm_host PRIVATE src/host/tm_d3d9_dxvk.cpp)
  set_source_files_properties(src/host/tm_d3d9_dxvk.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${TM_DXVK_PREFIX}/include/dxvk" COMPILE_OPTIONS "-std=c++17")
  target_compile_definitions(tm_host PRIVATE TM_HAVE_DXVK=1)
  target_link_libraries(tm_host PRIVATE ${DXVK_D3D9})
  set_target_properties(tm_host PROPERTIES BUILD_RPATH "${DXVK_LIBDIR}")
  find_program(XVFB_RUN xvfb-run)
  if(XVFB_RUN)
    # the game's own request: 1024x768 X1R5G5B5 full screen
    add_test(NAME dxvk_present COMMAND ${XVFB_RUN} -a -s "-screen 0 1024x768x24" $<TARGET_FILE:dxvk_present> 1024 768 24 0)
    set_tests_properties(dxvk_present PROPERTIES ENVIRONMENT "DXVK_WSI_DRIVER=SDL2;DXVK_LOG_LEVEL=warn" TIMEOUT 120)
    # the backend, through guest objects: tests/c/test_d3d9_dxvk.c
    add_executable(test_d3d9_dxvk tests/c/test_d3d9_dxvk.c src/host/tm_d3d9.c src/host/tm_loader.c src/host/tm_d3d9_dxvk.cpp
                   ${HLE_SOURCES})
    target_include_directories(test_d3d9_dxvk PRIVATE ${CMAKE_SOURCE_DIR}/src/host ${PCRECOMP}/runtime/recomp32 ${PCRECOMP}/runtime/win32hle)
    target_compile_definitions(test_d3d9_dxvk PRIVATE _GNU_SOURCE RECOMP_RETADDR=0u)
    target_compile_options(test_d3d9_dxvk PRIVATE -Wall -Wno-unused-function -fno-strict-aliasing)
    target_link_libraries(test_d3d9_dxvk PRIVATE ${DXVK_D3D9} PkgConfig::SDL2 Threads::Threads m)
    set_target_properties(test_d3d9_dxvk PROPERTIES BUILD_RPATH "${DXVK_LIBDIR}")
    add_test(NAME test_d3d9_dxvk COMMAND ${XVFB_RUN} -a -s "-screen 0 1024x768x24" $<TARGET_FILE:test_d3d9_dxvk>)
    set_tests_properties(test_d3d9_dxvk PROPERTIES ENVIRONMENT "DXVK_LOG_LEVEL=warn;SDL_AUDIODRIVER=dummy" TIMEOUT 120)
  endif()
endif()
