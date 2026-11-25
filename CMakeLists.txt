# =============================================================================
# The Movies (Movies.exe 1.1, Russian CD) on pcrecomp's win32hle.
#
# 64-bit host, guest memory "based" (the guest's 4 GB in one mapping).
# The same file builds for Linux x86-64, Linux arm64 and Android arm64-v8a
# (NDK toolchain file); see docs/BUILD.md.
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
set(HLE_PATCHED "${CMAKE_BINARY_DIR}/win32hle_patched")
file(MAKE_DIRECTORY "${HLE_PATCHED}")
find_program(PATCH_EXE patch REQUIRED)
foreach(pf hle_path.c hle_path_selftest.c)
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
list(FILTER HLE_SOURCES EXCLUDE REGEX "/hle_path\\.c$")
list(APPEND HLE_SOURCES "${HLE_PATCHED}/hle_path.c")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2 SDL2_ttf)
find_package(Threads REQUIRED)

add_library(tm_gen OBJECT ${GEN_SOURCES})
add_executable(tm_host src/host/tm_host.c src/host/tm_shell.c src/host/tm_user32.c src/host/tm_imports.c src/host/tm_itrace.c src/host/tm_kernel32.c src/host/tm_except.c src/host/tm_loader.c src/host/tm_dinput8.c src/host/tm_sysroot.c src/host/tm_vfs.c src/host/tm_wmvcore.c src/host/tm_dshow.c ${HLE_SOURCES}
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
# win32hle's own path selftest (patched by 0002), run against the patched hle_path.c
add_executable(hle_path_selftest ${HLE_PATCHED}/hle_path_selftest.c ${HLE_SOURCES})
target_include_directories(hle_path_selftest PRIVATE ${PCRECOMP}/runtime/recomp32 ${PCRECOMP}/runtime/win32hle)
target_compile_definitions(hle_path_selftest PRIVATE _GNU_SOURCE RECOMP_RETADDR=0u)
target_compile_options(hle_path_selftest PRIVATE -w -fno-strict-aliasing)
target_link_libraries(hle_path_selftest PRIVATE PkgConfig::SDL2 Threads::Threads m)
add_test(NAME hle_path_selftest COMMAND hle_path_selftest)
