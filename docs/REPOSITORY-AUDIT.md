# Host implementation and test coverage

## 1. Scope

This document describes the host implementation in src/host/, its
regression tests and known compatibility limits. Project-specific Win32
implementations supplement pcrecomp/win32hle; they do not implement the
complete Win32 API.

## 2. Host subsystems

| Subsystem | Implementation | Main validation |
|---|---|---|
| Bootstrap | tm_host.c, tm_host.h | M2 and M3 host runs |
| PE/module loading | tm_loader.c | test_loader, test_loader_audit |
| Virtual filesystem | tm_vfs.c | test_vfs, test_vfs_audit |
| Path translation | pcrecomp hle_path plus host mounts | hle_path_selftest, VFS tests |
| DirectInput 8 | tm_dinput8.c | test_dinput8 |
| DirectShow startup objects | tm_dshow.c | test_dshow, M3 boundary |
| Direct3D 9 guest interfaces | tm_d3d9.c, tm_d3d9_ifaces.h | test_d3d9 |
| DXVK Native adapter | tm_d3d9_dxvk.cpp | test_d3d9_dxvk, dxvk_present, M3 runtime |
| Exception diagnostics | tm_except.c | M2/M3 traces; guest dispatch is not implemented |
| Android entry point | tm_host_android.c | Android NDK compile and full-link workflow |

## 3. Loader and guest-pointer rules

Guest-visible pointers must refer to the guest's reserved memory region.
Host-only allocations remain behind opaque handles or internal host state.

The loader reserves address ranges for host DLL handles so that they cannot
alias guest heap allocations. Invalid module handles fail with
ERROR_MOD_NOT_FOUND; invalid export lookups fail with the corresponding
loader error.

The based-memory conversion hle_va(p) verifies that a host pointer lies
within the guest 4 GiB region. It does not validate that a guest address is
committed or belongs to a live allocation. hle_ptr(va) converts a guest VA
to a host address within the region; invalid or uncommitted accesses can
fault. Guest exception dispatch is not implemented.

## 4. Virtual filesystem

The host VFS maps Windows-style game paths onto configured host directories.
It performs case-insensitive lookup and translates Windows separators and
drive roots through the pcrecomp path layer.

Implemented compatibility behavior includes:

- directory enumeration and search-handle lifecycle;
- Windows-style error reporting for handled path and allocation failures;
- DOS wildcard normalization for the game's search masks;
- bounded path and mount-component handling;
- opaque search handles, separate from host pointers.

Known gaps include short 8.3 filename aliases, complete Windows path edge
cases, and wide-character file APIs. The measured game startup path has not
called the wide-character stubs.

## 5. Synchronization

Win32 shims run under pcrecomp/win32hle's machine lock. Host DLL and VFS
mount registration occurs before guest execution and is read-only during
the measured game path. A dedicated multithreaded stress test for all host
subsystems has not been recorded.

## 6. DirectShow and media

The host implements the COM objects needed by the game's startup probe.
This satisfies the observed startup contract and allows execution to
continue to Direct3D initialization.

This is not a media graph implementation. ASF playback, the game's
texture renderer, film export and full codec behavior remain unimplemented.
See DIRECTSHOW-STARTUP.md.

## 7. Direct3D 9

The guest D3D9 interfaces and vtables are represented in guest memory.
The host adapter validates guest objects and forwards supported operations
to DXVK Native. Unsupported methods stop with a diagnostic identifying
the interface method and caller rather than returning fabricated success.

The method-level implementation and test status is listed in
D3D9-REQUIREMENTS.md.

## 8. Exception handling

The host logs guest RaiseException arguments and stops the run. It does
not dispatch guest C++ exceptions or SEH. Hardware access violations are
host faults. See EXCEPTIONS.md.

## 9. Regression tests

Run the test suite from the configured build directory:

    ctest --test-dir build --output-on-failure

The repository includes unit tests for the installer, loader, VFS,
DirectInput, DirectShow startup objects, D3D9 interfaces and DXVK adapter,
plus pcrecomp guest-path self-tests. The M2 and M3 scripts exercise the
lifted executable against recorded runtime boundaries:

    python3 tests/m2_winmain.py --host build/tm_host --game /path/to/game
    python3 tests/m3_boundary.py --host build/tm_host --game /path/to/game

M2 verifies execution through the lifted CRT into WinMain. M3 verifies
the startup path through window creation and D3D9 device initialization.
These Linux tests do not validate Android runtime behavior.

## 10. Known limits

- Win32 coverage is limited to imports reached by the measured game path
  and explicitly implemented shims.
- Guest C++ EH and SEH dispatch are not implemented.
- Wide-character filesystem imports and some Windows path semantics are
  incomplete.
- DirectShow media playback and film export are not implemented.
- Android guest-memory reservation, PE mapping, import binding and full
  game execution remain unverified.
- Android touch, Activity lifecycle, audio/video and save/load remain
  unverified.
