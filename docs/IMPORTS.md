# Imports: what the game calls, and what answers it

The status of every Win32 import that
Movies.exe 1.1 (Russian CD) actually calls, and what has been checked about
each. The statuses are defined in `src/host/tm_imports.h`:

| Status | Meaning |
|---|---|
| implemented | our own implementation in `src/host`, written against the Windows documentation; an input it does not handle fails as Windows fails |
| diagnostic | lets the run go on, but not as Windows would; never on a critical path |
| win32hle-present | answered by pcrecomp's win32hle; not yet checked against this game's calls |
| win32hle-verified | answered by win32hle and checked against the calls recorded below, with a regression test |
| unsupported | no implementation; calling it stops the run, naming it |

An import is not counted as compatible because the run gets past it. It moves
to win32hle-verified only when its entry below lists the arguments the game
passes, the results and side effects Windows documents for them, the
GetLastError value, and a test in `tests/` that checks them.

## How the table is made

```bash
cd ~/tm/game && ~/tm/build/tm_host ~/tm/game --stop-at-winmain --user "$(mktemp -d)" --import-trace pre.json
cd ~/tm/game && ~/tm/build/tm_host ~/tm/game --watchdog 60 --user "$(mktemp -d)" --import-trace full.json
python3 ~/tm/the-movies-android/tools/imports_report.py pre.json full.json
```

`--import-trace` wraps every import bound through the IAT with a recording
trampoline (`src/host/tm_itrace.c`). For each called import it records the
number of calls and, for the first four, the first six stack arguments, the
return value, the bytes removed from the stack (arguments plus the return
address), GetLastError before and after, the calling lifted function and the
host thread. Tracing does not change the run: the function counts before
WinMain are the same with and without it.

Not covered: functions the game fetches with `GetProcAddress` (they bypass
the IAT; the CRT's probes for `FlsAlloc` and friends are logged by win32hle as
`not answered`), and COM interfaces (Direct3D 9, DirectSound), which get
their own trace in step 4 of docs/M3.md.

## Imports called, up to the current stop

Run from the game directory with an empty user directory holding an empty
`My Documents` (the main M2 scenario), until the run stops at the boundary
described below (`tests/m3_boundary.py`; the full trace is
[traces/m3_boundary_imports.json](traces/m3_boundary_imports.json)).

329 imports bound, 90 called (48 before WinMain).

| Import | Status | Calls | First called | First caller |
|---|---|---:|---|---|
| `ConvertSymbols` | implemented | 46 | before WinMain | `0x004035F0` |
| `GetModuleFileNameA` | implemented | 3 | before WinMain | `0x00A45266` |
| `GetModuleHandleA` | implemented | 6 | before WinMain | `0x00A31571` |
| `GetProcAddress` | implemented | 7 | before WinMain | `0x00A488E9` |
| `SHGetFolderPathW` | implemented | 8 | before WinMain | `0x00950190` |
| `CreateMutexA` | win32hle-present | 3 | before WinMain | `0x00A034B0` |
| `EnterCriticalSection` | win32hle-present | 988 | before WinMain | `0x00A36089` |
| `FreeEnvironmentStringsA` | win32hle-present | 1 | before WinMain | `0x00A45308` |
| `GetACP` | win32hle-present | 1 | before WinMain | `0x00A427E1` |
| `GetCommandLineA` | win32hle-present | 1 | before WinMain | `0x00A31571` |
| `GetCPInfo` | win32hle-present | 2 | before WinMain | `0x00A42641` |
| `GetCurrentDirectoryA` | win32hle-present | 13 | before WinMain | `0x00A2F6FB` |
| `GetCurrentProcessId` | win32hle-present | 1 | before WinMain | `0x00A455A2` |
| `GetCurrentThreadId` | win32hle-present | 3 | before WinMain | `0x00A37F62` |
| `GetEnvironmentStrings` | win32hle-present | 1 | before WinMain | `0x00A45308` |
| `GetEnvironmentStringsW` | win32hle-present | 1 | before WinMain | `0x00A45308` |
| `GetFileType` | win32hle-present | 30 | before WinMain | `0x00A41A46` |
| `GetLastError` | win32hle-present | 998290 | before WinMain | `0x00A45308` |
| `GetLocaleInfoA` | win32hle-present | 3 | before WinMain | `0x00A48F49` |
| `GetStartupInfoA` | win32hle-present | 2 | before WinMain | `0x00A41A46` |
| `GetStdHandle` | win32hle-present | 3 | before WinMain | `0x00A41A46` |
| `GetStringTypeA` | win32hle-present | 1 | before WinMain | `0x00A49155` |
| `GetStringTypeW` | win32hle-present | 1 | before WinMain | `0x00A49155` |
| `GetSystemTimeAsFileTime` | win32hle-present | 1 | before WinMain | `0x00A455A2` |
| `GetTickCount` | win32hle-present | 1 | before WinMain | `0x00A455A2` |
| `GetVersionExA` | win32hle-present | 2 | before WinMain | `0x00A31571` |
| `HeapAlloc` | win32hle-present | 269503 | before WinMain | `0x00A3473B` |
| `HeapCreate` | win32hle-present | 1 | before WinMain | `0x00A454CC` |
| `HeapFree` | win32hle-present | 236158 | before WinMain | `0x00A31A7F` |
| `HeapReAlloc` | win32hle-present | 6 | before WinMain | `0x00A34874` |
| `HeapSize` | win32hle-present | 620 | before WinMain | `0x00A3D335` |
| `InitializeCriticalSection` | win32hle-present | 46 | before WinMain | `0x00A488D9` |
| `InterlockedIncrement` | win32hle-present | 2 | before WinMain | `0x00A2B719` |
| `LCMapStringA` | win32hle-present | 2 | before WinMain | `0x00A3591B` |
| `LCMapStringW` | win32hle-present | 1 | before WinMain | `0x00A3591B` |
| `LeaveCriticalSection` | win32hle-present | 988 | before WinMain | `0x00A35FBC` |
| `LockResource` | win32hle-present | 1 | before WinMain | `0x00A41A46` |
| `QueryPerformanceCounter` | win32hle-present | 1 | before WinMain | `0x00A455A2` |
| `RegCloseKey` | win32hle-present | 15 | before WinMain | `0x009896B0` |
| `RegCreateKeyExA` | win32hle-present | 15 | before WinMain | `0x009896C0` |
| `RegQueryValueExA` | win32hle-present | 15 | before WinMain | `0x00989790` |
| `RegSetValueExA` | win32hle-present | 7 | before WinMain | `0x00989830` |
| `SetConsoleCtrlHandler` | win32hle-present | 1 | before WinMain | `0x00A34421` |
| `SetLastError` | win32hle-present | 998250 | before WinMain | `0x00A37D6F` |
| `SetUnhandledExceptionFilter` | win32hle-present | 1 | before WinMain | `0x00A381F6` |
| `TlsAlloc` | win32hle-present | 1 | before WinMain | `0x00A37D36` |
| `TlsGetValue` | win32hle-present | 998251 | before WinMain | `0x00A37D6F` |
| `TlsSetValue` | win32hle-present | 2 | before WinMain | `0x00A37F62` |
| `MessageBoxW` | diagnostic | 2 | after WinMain | `0x005303C0` |
| `CreateDirectoryA` | implemented | 43 | after WinMain | `0x00A2EBD7` |
| `CreateFileA` | implemented | 28 | after WinMain | `0x00A489E0` |
| `DeleteFileA` | implemented | 1 | after WinMain | `0x00A329D4` |
| `FindFirstFileA` | implemented | 178 | after WinMain | `0x00A33368` |
| `FindNextFileA` | implemented | 716 | after WinMain | `0x0095AAC0` |
| `GetDiskFreeSpaceExW` | implemented | 1 | after WinMain | `0x00992DC0` |
| `GetFullPathNameA` | implemented | 1 | after WinMain | `0x00A480C3` |
| `GetSystemDirectoryA` | implemented | 1 | after WinMain | `0x0093C640` |
| `GetTempPathA` | implemented | 1 | after WinMain | `0x00964740` |
| `LoadLibraryA` | implemented | 2 | after WinMain | `0x00990730` |
| `SHGetSpecialFolderPathA` | implemented | 1 | after WinMain | `0x009F9D20` |
| `CloseHandle` | win32hle-present | 20 | after WinMain | `0x00A41F50` |
| `CoCreateInstance` | win32hle-present | 1 | after WinMain | `0x009AD7A0` |
| `CoInitialize` | win32hle-present | 2 | after WinMain | `0x009F9950` |
| `CoUninitialize` | win32hle-present | 1 | after WinMain | `0x009AD7A0` |
| `CreateEventA` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `CreateThread` | win32hle-present | 1 | after WinMain | `0x00A2ECC9` |
| `CreateWindowExA` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `DefWindowProcA` | win32hle-present | 6 | after WinMain | `0x0052FCD0` |
| `FileTimeToLocalFileTime` | win32hle-present | 27 | after WinMain | `0x00A33368` |
| `FileTimeToSystemTime` | win32hle-present | 27 | after WinMain | `0x00A33368` |
| `FindClose` | win32hle-present | 29 | after WinMain | `0x00A33368` |
| `GetClientRect` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `GetStockObject` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `GetTempFileNameA` | win32hle-present | 1 | after WinMain | `0x00964740` |
| `GetTimeZoneInformation` | win32hle-present | 1 | after WinMain | `0x00A40F71` |
| `GetVersion` | win32hle-present | 1 | after WinMain | `0x00556820` |
| `InterlockedExchange` | win32hle-present | 1 | after WinMain | `0x00BFA1A8` |
| `LoadCursorA` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `LoadIconA` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `ReadFile` | win32hle-present | 29 | after WinMain | `0x00A44B74` |
| `RegisterClassA` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `SetCurrentDirectoryA` | win32hle-present | 2 | after WinMain | `0x00A2FE35` |
| `SetCursor` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `SetEnvironmentVariableA` | win32hle-present | 2 | after WinMain | `0x00A2FE35` |
| `SetFilePointer` | win32hle-present | 69 | after WinMain | `0x00A44974` |
| `SetRect` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `ShowWindow` | win32hle-present | 1 | after WinMain | `0x005303C0` |
| `WaitForSingleObject` | win32hle-present | 1 | after WinMain | `0x0052FC80` |
| `WideCharToMultiByte` | win32hle-present | 10 | after WinMain | `0x00A40F71` |
| `WriteFile` | win32hle-present | 1 | after WinMain | `0x00A446FB` |

None of the win32hle-present imports has been checked yet. The high counts
(`GetLastError`, `SetLastError`, `TlsGetValue`) come from the CRT's
per-thread data lookup, which saves and restores the last error around every
`TlsGetValue`. Functions reached through `GetProcAddress` (such as
`DirectInput8Create`) and COM methods are not in this table; they have their
own sections below.

## Findings from the trace

**The current directory changes the start-up path.** `GetCurrentDirectoryA`
returns the host's working directory mapped through the drives (`C:\` in the
game directory, `Z:\<host path>` elsewhere). The CRT start-up code takes a
different path for each, and outside drive C: it also calls `FindFirstFileA`
before WinMain. Measured counts are in docs/M2.md 2. The regression tests run
from the game directory.

**Registry values the game expects are missing.** win32hle's registry is in
memory, starts empty and is not saved (`tm_host` gives it no file). Before
WinMain the game opens or creates `Software\Lionhead Studios Ltd\TheMovies` three
times (in two spellings; the root key is not in the trace) and queries four values, all
missing in this run: `First Time:`, `SC1`, `SC2` and `Hdr`. The
installer's MSI `Registry` table creates only the two empty keys `Software`
and `Software\Lionhead Studios Ltd` under the current user, so these values
do not come from that table. Where they come from on a real installation (an
installer custom action, or the game itself on an earlier run: it calls
`RegSetValueExA` 4 times before WinMain) is not yet established. Until it is,
nothing is seeded: a first-launch run is what is tested.

**A development leftover.** The game opens
`Software\Perforce\environment` and queries `P4CONFIG` twice (missing). That
is a Perforce client-library lookup in the shipped build; it affects nothing
when the value is missing, as on a normal player's machine.

**Passed: `GetDiskFreeSpaceExW`.** Called from `sub_00992DC0` after the
first-launch message box, with `D:\My Documents\`. It is implemented (below)
and the call succeeds.

**Passed: the delay-load failure (`0xC06D007E`).** See "Dynamically loaded
DLLs" below for the evidence and the fix.

**Fixed: temporary files were written into the game directory.** win32hle's
`GetTempPathA` answers `C:\WINDOWS\TEMP\`, and C: is the game directory.
The game calls `GetTempPathA` from `sub_00964740`, then `GetTempFileNameA`,
`WriteFile` and `DeleteFileA`, and lists `<temp>\The Movies\`. So each run
created `WINDOWS\TEMP\The Movies` inside the game install. The M2 test could
not see it because it stops at WinMain. `GetTempPathA` is now our own (below),
and `tests/m3_boundary.py` checks that the game directory is unchanged after
the whole run. The directories the earlier runs created (empty) were removed
from the local install.

## Dynamically loaded DLLs

### The delay-load import directory

Movies.exe has a delay-load import directory (`DataDirectory[13]`, RVA
`0x0099B8B0`, 128 bytes) with three DLLs. All descriptors have
`grAttrs = 1` (RVAs, the VC7+ form).

| DLL | Imports | Module handle slot | IAT |
|---|---|---|---|
| `WMVCore.DLL` | `WMCreateSyncReader`, `WMCreateProfileManager` | `0x01014E38` | `0x00ED373C` |
| `DINPUT8.dll` | `DirectInput8Create` | `0x01016F38` | `0x00ED3734` |
| `d3d9.dll` | `Direct3DCreate9` | `0x01016F78` | `0x00ED3748` |

Each import has a thunk that loads its slot's address into eax and jumps to
the delay-load helper, `__delayLoadHelper2` (`sub_00BFA1A8`). The helper
calls `LoadLibraryA` and `GetProcAddress` and raises `0xC06D007E`/`7F` if
either fails, since the game installs no failure hook.

### What caused 0xC06D007E

`src/host/tm_except.c` now decodes every `RaiseException` before the run
stops. For the delay-load codes it reads the `DelayLoadInfo` passed in
`ExceptionInformation[0]`, and the stage comes from that record, not from
the code. The record from the failing run:

| Field | Value |
|---|---|
| `DelayLoadInfo` | `0x103FFC60`, `cb` = `0x24` |
| `szDll` | `DINPUT8.dll` |
| `dlp` | by name: `DirectInput8Create` |
| `ppfn` (IAT slot) | `0x00ED3734` |
| `hmodCur` | 0: `LoadLibraryA` returned NULL (stage `dliFailLoadLib`) |
| `pfnCur` | 0 (GetProcAddress never reached) |
| `dwLastError` | 126 (`ERROR_MOD_NOT_FOUND`) |
| descriptor | `0x00D9B8D0`, `grAttrs` 1, hmod slot still 0 |

The caller is `sub_009F9950`, the game's input start-up. It calls the
import stub `0x00A4E04B`, whose first call goes through the thunk at
`0x00A4E030` into the helper. The helper called
`LoadLibraryA("DINPUT8.dll")` (the import trace records it, from
`0x00BFA1A8`). win32hle's loader answered NULL and 126 without looking at
any file: `dinput8` is not one of its system DLLs, and no file of that name
is in the application directory. The module that failed is the one the game
asked for, not a dependency: win32hle does not load DLL code, so a host DLL
has no dependencies to resolve. Execution stopped in the helper's
`RaiseException`, because win32hle does not dispatch guest exceptions.

No other exception was raised before it (`RaiseException` calls: 1). None
occurred during DLL initialisation, since no DLL code is run.

### Loader semantics

Movies.exe imports `LoadLibraryA`, `GetModuleHandleA`, `GetProcAddress`,
`FreeLibrary` and `GetModuleFileNameA`. It has no W or Ex forms, does not
import `LoadLibraryExA`, `SetDllDirectoryA`, `SetSearchPathMode` or
`AddDllDirectory`, and has no manifest resource (checked in the
executable). So the behaviour to emulate is plain `LoadLibraryA` on the
Windows of its time: Windows XP SP2 and later with SafeDllSearchMode on (the
default since SP2), no altered search path, no activation context. It is not
the order of Windows 10/11 with `LOAD_LIBRARY_SEARCH_*` flags.

`src/host/tm_loader.c` puts the five functions in front of win32hle's:

| Point | Windows (XP SP2+, `LoadLibraryA`) | Before (win32hle) | Now |
|---|---|---|---|
| a module already loaded under that base name | used, whatever its directory | yes | yes |
| search order for a bare name | application dir, system dir, 16-bit system dir, Windows dir, current dir, PATH | its system DLLs, then the current dir, then the application dir | application dir; `C:\WINDOWS\SYSTEM32`, `C:\WINDOWS\SYSTEM`, `C:\WINDOWS` (in each, a host DLL of that directory before a file there); then win32hle's system DLLs and its search (current dir; the guest has no PATH) |
| a name with a path | that path only | resolved as given | the same, after making it absolute the Windows way (`..`, `/`, relative names); a path to a host DLL's directory names the host DLL |
| `.dll` added when no extension; a trailing `.` = none | yes | `.dll` added | both |
| file names | case-insensitive | yes (hle_path) | yes |
| reference counts | `LoadLibrary` +1, `FreeLibrary` -1, unloaded at 0 | never below 1, `FreeLibrary` always TRUE | host DLLs: as Windows; a stale handle fails with `ERROR_MOD_NOT_FOUND` |
| `DllMain` attach / detach | at the first load / the last `FreeLibrary`; a FALSE attach fails the load with `ERROR_DLL_INIT_FAILED` | DLL files mapped, code never run | host DLLs: `on_load` / `on_unload`, the same rules (dinput8 and wmvcore have none) |
| a file that is not a loadable image | `ERROR_BAD_EXE_FORMAT` | | a host DLL entry with `loadable = 0` |
| `GetModuleHandleA` | loaded modules only, no reference | any system DLL, loaded or not | host DLLs: only while loaded |
| `GetProcAddress` of a name the DLL does not export | NULL, `ERROR_PROC_NOT_FOUND` | the same | the same, and also for an export of the real DLL this host does not implement; the run logs which, and lists every export asked for at exit |
| dependencies of a loaded DLL | loaded by the same search | DLL files mapped, imports not resolved | host DLLs have no imports; no third-party DLL code runs |

Outside the current scope, with the reason:
- **KnownDLLs**: Windows takes these from the system directory even if the
  application directory has a copy. The game directory holds one DLL
  (`helper.dll`), which is not a system DLL, so the difference cannot arise
  with this install.
- **`.local` redirection and side-by-side assemblies**: the executable has
  no manifest, and the install has no `.local` file.
- **`SetDllDirectory`, `LoadLibraryEx` flags, the Windows 8+ search
  functions**: not imported.
- **A per-drive current directory**: `C:name` is taken against the one
  current directory when it is on C:, else against the drive's root.
- win32hle's own system DLLs (`kernel32.dll`, `user32.dll`, ...) keep its
  rules: `GetModuleHandleA` answers for them even when the game never
  loaded them, and they are **not listed** in `C:\WINDOWS\SYSTEM32` (only
  host DLLs are). Nothing in the trace looks for them as files.

A DLL file found on disk is mapped by win32hle, but its exports are not
answered (`GetProcAddress` gives NULL, `ERROR_PROC_NOT_FOUND`). So a real DLL
placed in the game directory would fail at `dliFailGetProc`, visibly.

Tests: `tests/c/test_loader.c` (search order, `.dll` and trailing-dot
names, case, an application-dir copy taking precedence, reference counts,
unloading, stale handles, exports by name and ordinal,
`GetModuleFileNameA` truncation, a missing DLL) and `tests/c/test_vfs.c`
(below).

### Host DLLs

A host DLL is a system DLL this host implements itself. Each one
(`tm_host_dll` in `src/host/tm_loader.h`) has:

| Field | Meaning |
|---|---|
| `name`, `dir` | its file name and guest directory (`C:\WINDOWS\SYSTEM32`) |
| `visible` | listed in that directory by `FindFirstFileA`, `GetFileAttributesA` (a virtual entry: nothing on disk) |
| `loadable` | `LoadLibraryA` gives a module; if not, `ERROR_BAD_EXE_FORMAT` |
| `exports` | each export of the real DLL that this host knows about, with a shim when it is implemented, or a reason when it is not |
| handle, references | a pseudo-module at `0x7D000000 + 0x10000 * index`, counted as above |
| `on_load`, `on_unload` | attach at the first reference (may fail the load), detach at the last `FreeLibrary` |

The file system and the loader read the same table, so a name is answered
the same way by both: a visible host DLL is the file `FindFirstFileA` finds
in `C:\WINDOWS\SYSTEM32`, and the module `LoadLibraryA` loads from there.
Four questions stay separate, and `tests/c/test_vfs.c` tests each: is the
file there; does the module load; is the name an export; is that export
implemented.

| Host DLL | Visible | Loadable | Exports |
|---|---|---|---|
| `dinput8.dll` | yes | yes | `DirectInput8Create`: implemented (below) |
| `wmvcore.dll` | yes | yes | `WMCreateProfileManager`, `WMCreateSyncReader`: **not implemented**. `GetProcAddress` fails with `ERROR_PROC_NOT_FOUND`; through the game's delay-load helper that is `0xC06D007F` (`dliFailGetProc`), decoded and reported by `tm_except.c` |

`wmvcore.dll` is there for the game's start-up check, which looks for the
file. It is not an implementation of the Windows Media Format SDK, and
neither of its exports is called at start-up (checked: no `GetProcAddress`
for either in the run).

### imagehlp.dll

The game loads it in `sub_00990730`, which is reference counted. That
function takes six functions with `GetProcAddress`: `SymInitialize`,
`SymSetOptions`, `SymGetModuleBase64`, `SymFromAddr`, `StackWalk64` and
`SymGetLineFromAddr64`. It returns false if the DLL or any one of them is
missing. Its only caller is a virtual method, `sub_00991530`, of the class
RTTI names `IStackTrace` / `MV::CStackTrace` (vtable `0x00CC9BF4`). The
other slots of that class walk the stack (`sub_00992870`) and turn
addresses into names and lines (`sub_009909F0`, `sub_00990AB0`). The object
is built from `sub_0052FFE0`, beside WinMain. So imagehlp serves the game's
own stack-trace reporting. Its failure makes the initialisation return
false, and start-up goes on (observed: nothing after it depends on it).

On a real Windows, imagehlp.dll is always present, so this differs from
Windows: if the game ever writes a stack trace, it has no symbols. It is
left unsupported (the load fails with 126) and not replaced by dbghelp.
Implementing it means real stack walking over lifted code, where the guest
stack has no native frames to walk, and nothing on the start-up path needs
it. Noted for crash diagnostics later. (The failure path calls
`FreeLibrary(NULL)` instead of the module, a bug in the game; it is
harmless.)

### DirectInput 8

What the game calls, from `sub_009F9950` (one run, one thread):

1. `CoInitialize(NULL)`.
2. `DirectInput8Create(GetModuleHandleA(NULL), 0x0800, IID_IDirectInput8A, &di, NULL)`.
   It returns early if this fails.
3. `di->CreateDevice(GUID_SysKeyboard, &keyboard, NULL)`, then on the
   keyboard: `SetDataFormat(c_dfDIKeyboard)` (256 key objects, 256 bytes;
   the table is linked into the exe from `dinput8.lib`),
   `SetCooperativeLevel([0x00FA7128], DISCL_EXCLUSIVE | DISCL_FOREGROUND)`,
   `SetProperty(DIPROP_BUFFERSIZE, {DIPH_DEVICE, 128})` and `Acquire()`.
4. The same for `GUID_SysMouse` with `c_dfDIMouse` (3 axes, 4 buttons,
   16 bytes).

Only the results of steps 2 and 3's `CreateDevice` are checked. The
window handle at `0x00FA7128` is still 0 at this point: the main window
does not exist yet. So `SetCooperativeLevel` fails with `E_HANDLE`, as it
does on Windows for a NULL window with exclusive/foreground flags. The device
keeps the default level, and `Acquire` succeeds under it (non-exclusive,
background, as Wine's dinput).

| Function or method | Status | Supported | Not supported, limits | Exercised by the game | Test |
|---|---|---|---|---|---|
| `DirectInput8Create` | implemented | `IID_IDirectInput8A` (and `IID_IUnknown`), version 0x0800; `E_POINTER`, `E_NOINTERFACE`, `DIERR_NOAGGREGATION`, `DIERR_INVALIDPARAM` (NULL instance), `DIERR_OLD/BETADIRECTINPUTVERSION`; `*ppvOut` NULL on failure | `IID_IDirectInput8W` fails with `E_NOINTERFACE` (not implemented; the game uses A) | yes, DI_OK | test_dinput8 |
| `IDirectInput8A` QueryInterface, AddRef, Release | implemented | IUnknown and its own IID; reference counts; a device holds its DirectInput object | | QI/AddRef no; Release not yet | test_dinput8 |
| `IDirectInput8A::CreateDevice` | implemented | `GUID_SysKeyboard`, `GUID_SysMouse`; `E_POINTER`, `DIERR_NOAGGREGATION`, `DIERR_DEVICENOTREG` for any other device | no joysticks or other devices | yes, both S_OK | test_dinput8 |
| `IDirectInputDevice8A` QueryInterface, AddRef, Release | implemented | IUnknown and `IID_IDirectInputDevice8A`; Release unacquires and frees at 0 | | not yet | test_dinput8 |
| `SetDataFormat` | implemented | `dwSize` 24, `dwObjSize` 16, data size a multiple of 4, every object inside it; `DIERR_ACQUIRED` | the object list is checked, not yet used (no input to place) | yes, DI_OK | test_dinput8 |
| `SetCooperativeLevel` | implemented | exactly one of exclusive/non-exclusive and foreground/background; NULL window only for non-exclusive background (the desktop); a top-level window; `DIERR_UNSUPPORTED` for exclusive background; `DIERR_ACQUIRED` | exclusive mode has no effect (no cursor hiding or capture: no display) | yes, E_HANDLE (no window yet) | test_dinput8 |
| `SetProperty` | implemented for `DIPROP_BUFFERSIZE` | a `DIPROPDWORD` with `DIPH_DEVICE`; `DIERR_INVALIDPARAM`, `DIERR_ACQUIRED` | any other property stops the run, naming it | yes, DI_OK | test_dinput8 |
| `Acquire`, `Unacquire` | implemented | needs a data format; `DI_NOEFFECT` when already in that state; foreground level: `DIERR_OTHERAPPHASPRIO` unless the window is the foreground window | | Acquire yes, DI_OK; Unacquire not yet | test_dinput8 |
| every other method (`GetDeviceState`, `GetDeviceData`, `EnumDevices`, `Poll`, ...) | unsupported | | a named stub: calling it stops the run | no | test_dinput8 checks the stubs are named |

**Input does not work yet.** The devices are created and acquired, but the
host has no keyboard or mouse source, and `GetDeviceState`/`GetDeviceData`
are not implemented. A successful DirectInput start-up means only that the
game's initialisation sequence ran with correct results.

The HRESULTs come from the DirectInput 8 documentation. Where it is silent,
they follow Wine's dinput: the order of the checks in `DirectInput8Create`,
the default cooperative level, the desktop for a NULL window, and a device
holding a reference to its DirectInput object. None of this has been
checked on real Windows.

### GetTempPathA (implemented)

It returns `D:\Local Settings\Temp\` (created if missing). That is the
per-user temporary directory of Windows XP under the profile; here the
profile is the user drive D:, where the shell folders already are. Windows
takes `%TMP%`, `%TEMP%`, `%USERPROFILE%` or the Windows directory, and
the guest has none of these variables. Return value: the length without
the terminator if it fits, else the size needed including it, with the
buffer untouched. Test: `tests/c/test_loader.c`.

### The Windows directory

Before: win32hle answered `C:\WINDOWS` and `C:\WINDOWS\SYSTEM32` for the
Windows and system directories, and C: is the game directory. So both lived
inside the game install, and anything written there (temporary files, for
one) changed the install.

Now three host directories are kept apart, and `tm_host` refuses to start if
one is inside another (`--user DIR`, `--system DIR`):

| Guest | Host | Written by a run |
|---|---|---|
| `C:\` | the game install | never (checked by `tests/m3_boundary.py` and `tests/m2_winmain.py`) |
| `C:\WINDOWS`, with `SYSTEM32` in it | the runtime system directory (`--system`, default `./tm_system`) | only what the game itself writes there |
| `D:\` | the user directory (`--user`): profile folders, saves, `Local Settings\Temp` | yes |

`C:\WINDOWS` is a mount over that part of C:. A guest path at or under it
resolves in the system directory, and a host path in the system directory
maps back to `C:\WINDOWS\...`. The rest of C: is unchanged, so the game
still sees itself as `C:\Movies.exe` and its current directory as `C:\`.
`GetWindowsDirectoryA` and `GetSystemDirectoryA` are our own, with the
documented length / size-needed contract.

The system directory starts with an empty `SYSTEM32`. No DLL files are put
in it.

### The virtual file system

Mounts and host DLLs are entries that have no directory entry on disk.
`src/host/tm_vfs.c` makes them part of what the game sees:

| Entry | Where it is listed | What it is |
|---|---|---|
| a mount (`C:\WINDOWS`) | its parent (`WINDOWS` in `C:\`) | a directory with the host directory's times |
| a visible host DLL (`wmvcore.dll`, `dinput8.dll`) | its directory (`C:\WINDOWS\SYSTEM32`) | a file: `FILE_ATTRIBUTE_ARCHIVE`, size 0, the directory's times |

A real entry of the same name, in any case, is hidden by the virtual one: a
listing never shows a name twice. For example, a real `C:\windows` folder
in a game directory would not appear next to the mount.

All paths are first made absolute the Windows way, then matched. This is a
patch to win32hle's `hle_path.c` (`patches/pcrecomp/0001`, applied to a copy
in the build tree; the pcrecomp checkout is not modified):
- a relative name is taken against the guest's current directory;
- `C:name` is taken against it when it is on C:, else against `C:\`;
- `\name` is on the current drive;
- `.` is dropped, and `..` removes a component and stops at the drive's
  root;
- trailing dots and spaces of the last component go.

So `C:\Data\..\WINDOWS\TEMP` is in the system directory, and
`C:\WINDOWS\..\Movies.exe` is the game's file. A write can't reach the game
directory through the mount. (Before this, a relative `WINDOWS\TEMP` from
`C:\` would have been created inside the game directory.) Mounts may not
overlap, on either the guest or the host side: `tm_vfs_mount` refuses
`C:\WINDOWS\SYSTEM32` while `C:\WINDOWS` is mounted, and the reverse.

| Function | Behaviour now |
|---|---|
| `FindFirstFileA`, `FindNextFileA` | our own listing: the host directory's names and the virtual entries, merged without duplicates, in NTFS's order (by name without case, `.` and `..` first). A drive's root has no `.` and `..`. `*.` matches names without an extension. A missing directory fails with `ERROR_PATH_NOT_FOUND`, a missing name with `ERROR_FILE_NOT_FOUND`, a bad handle with `ERROR_INVALID_HANDLE`. `FindClose` is win32hle's (it closes any handle) |
| `GetFileAttributesA` | a host DLL entry: `FILE_ATTRIBUTE_ARCHIVE`; otherwise win32hle's (a mount is its host directory: `FILE_ATTRIBUTE_DIRECTORY`) |
| `GetFullPathNameA` | the normalisation above, as a guest path; the length, or the size needed with the buffer untouched; the file part, NULL after a trailing `\`. win32hle returned a host path for a relative name. UNC names are left to win32hle |
| `CreateFileA`, `DeleteFileA`, `CreateDirectoryA` | win32hle's, except on a host DLL entry: it has no contents, so opening or deleting it fails with `ERROR_ACCESS_DENIED`, and creating a directory of its name fails with `ERROR_ALREADY_EXISTS` |
| `SetCurrentDirectoryA`, `GetCurrentDirectoryA` | win32hle's, through the patched path code: `C:\WINDOWS` is the system directory and comes back as `C:\WINDOWS` |

Differences from Windows, all documented here:
- A host DLL entry has no bytes and no version resource. Windows would open
  the real file. The game does not open these files in any traced run.
- Short (8.3) names are not generated (`cAlternateFileName` is empty).
- `MoveFileA` and `CopyFileA` know nothing of host DLL entries. The game
  doesn't import `CopyFileA` or `MoveFileA` on the start-up path.

Tests:
- `tests/c/test_vfs.c` (74 checks): the root listing (`WINDOWS` in `C:\*`,
  `c:/w*`, a relative `*`, a real `windows` folder hidden), nested
  listings, the real and virtual entries in `SYSTEM32` merged, `*.`, error
  codes, attributes, translation through `..`, relative names, `C:name`
  and `\name`, `GetFullPathNameA`, writes kept out of the game directory,
  host DLL entries refused for open, delete and replace, mount overlap,
  and the four DLL questions for a test DLL and for `wmvcore.dll` and
  `dinput8.dll`.
- `tests/c/test_sysroot.c`: separation and the directory functions.
- `hle_path_selftest`: win32hle's own path test against the patched file.
  One of its checks expected a relative `..\file.txt` to stay relative to
  the host's cwd. The patch changes that on purpose (the Windows rule), so
  `patches/pcrecomp/0002` changes that one expectation, with the reason.

### The current boundary

After DirectInput, WinMain calls `sub_0093C640`, the game's Windows Media
check. It has two parts. Both must succeed, or it shows its warning box
(type `0x30`, saying that a newer Windows Media Player and its codecs are
needed) and WinMain goes to its exit path.

1. `GetSystemDirectoryA`, then `<system dir>\wmvcore.dll` and
   `<system dir>\dinput8.dll` must exist (`sub_00964190`, through
   `FindFirstFileA`). **This now succeeds**: both are visible host DLLs.
2. `sub_009AD7A0`, a DirectShow probe (new finding from the executable,
   reached for the first time in this run):
   - `CoInitialize(NULL)`;
   - `CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder)`;
   - `CoCreateInstance(CLSID_CaptureGraphBuilder2, ..., IID_ICaptureGraphBuilder2)`;
   - `ICaptureGraphBuilder2::SetFiltergraph(graph)`;
   - `CoCreateInstance(CLSID_WMAsfWriter, ..., IID_IBaseFilter)`;
   - `IFilterGraph::AddFilter(writer, L"ASF Enc/Writer")`;
   - Release of all three, then `CoUninitialize`.

   It returns true only if every call succeeds. The first one fails here:
   win32hle's `CoCreateInstance` has no `CLSID_FilterGraph`
   (`REGDB_E_CLASSNOTREG`). So the check fails, correctly.

This corrects what this file said before ("start-up needs `wmvcore.dll`
only to exist"). That was read from the first part alone. Start-up also
needs the DirectShow filter graph, the capture graph builder, and the
Windows Media ASF Writer filter to be creatable, and the writer to be
addable to a graph. Those are the objects of the film-export path.

After the box, as before, the run ends with a SIGSEGV on the game's exit
path: a read of guest address `0x00000004` in `sub_00936350`, from
`sub_0092FB20`. Whether Windows takes the same fault there has not been
checked.

The main window has not been created at this point. One window exists
(`ShowWindow` on handle `0x00010010`), and the main-window variable at
`0x00FA7128` is still 0. So DirectInput's `SetCooperativeLevel` still
fails with `E_HANDLE` for both devices, as it should for a NULL window.

`tests/m3_boundary.py` holds this boundary; the run is reproducible.

## Windows Media and DirectShow

### What our executable uses, and when

From our own binary (docs/EXTERNAL-REFERENCES.md has the comparison with
lifesupport-TheMovies):

| Task | What | Where | Reached | Status |
|---|---|---|---|---|
| A. start-up component check | `wmvcore.dll` and `dinput8.dll` present in the system directory; DLL resolution | `sub_0093C640` part 1 | every start | **done**: visible host DLLs |
| B. DirectShow | `CLSID_FilterGraph` (`IGraphBuilder`), `CLSID_CaptureGraphBuilder2`, and for the videos (112 `.wmv` files: logos, intro, menu loop, credits, tutorials) `CLSID_WMAsfReader`, the filters it needs and real playback (`sub_0097E190`, from `CMediaSys@MV` and `CMoviePlayerSys@MV`) | start-up probe (`sub_009AD7A0`); videos before and in the menu | every start (the probe) | not implemented; **blocks start-up** |
| C. Windows Media Format profile manager, with the ASF writer | `CLSID_WMAsfWriter` as an `IBaseFilter` (start-up probe; film export); `WMCreateProfileManager`, `CreateEmptyProfile(WMT_VER_7_0)`, `LoadProfileByData` with the `.prx` XML (`sub_009AFCB0`) and the export graph (`CAviSyst@MV` slot 1) | the probe creates the writer filter; the profile manager only on export | the writer at every start; the rest on export | not implemented; the writer filter **blocks start-up** |
| D. synchronous reader | `WMCreateSyncReader` and the `IWMSyncReader` methods the audio engine's WMA decoder calls (`sub_00BAAB70`) | when a `.wma` file is opened (the player's own music; the CD's music is `.ogg`) | not at start-up | not implemented; its interfaces and formats are to be traced from the call graph when a test opens a `.wma` file |

No decoder library is used yet. If FFmpeg is used later, its exact
`configure` line and enabled components are recorded here: GPL or nonfree
options change its licence (ffmpeg.org/legal.html).

### Implementation options for C and D

| | A. Own minimal implementation | B. Adapt lifesupport-TheMovies | C. FFmpeg-backed implementation |
|---|---|---|---|
| What | profile manager with the methods the game calls, plus real `IWMProfile`/`IWMStreamConfig` objects from the parsed `.prx`. Sync reader only once WMA decoding exists | port its `profile_mgr.c` / `sync_reader.c` | A, plus libavformat/libavcodec behind the sync reader (ASF demux, WMA decode), later the DirectShow reader for the `.wmv` files and the export writer |
| Effort | profile manager: small | small to read, but a rewrite anyway for our COM model | sync reader: moderate; DirectShow playback and export: large |
| Fit with the CD exe | exact: built from our call graph | its call graph is from another binary | as A |
| Dependencies | none | FFmpeg for its reader | FFmpeg, configuration to be recorded |
| Licence | ours (MIT) | KPC LifeSupport: attribution, no sale; cannot be relicensed MIT | per the recorded FFmpeg build |

Its findings are used, its code is not.

### What the DirectShow probe needs

For the probe to succeed, its six calls need real objects:
- a filter graph that keeps its filters (`AddFilter` stores the filter and
  calls its `JoinFilterGraph`, and `Release` frees them);
- a capture graph builder that stores the graph it is given;
- an ASF writer filter whose `IBaseFilter` basics are real (reference
  counts, `JoinFilterGraph`, state, `QueryFilterInfo`).

Encoding through the writer (its pins, `IConfigAsfWriter`, the profile)
belongs to task C. With only these objects the probe would pass while film
export does not work yet. Not implemented.

## GetDiskFreeSpaceExW (implemented)

`src/host/tm_kernel32.c`; test `tests/c/test_disk_free_w.c` (54 checks:
`ctest --test-dir build`).

| Input | Result |
|---|---|
| a directory on a mapped drive (root or below; `\` or `/`; trailing separator or not; any case; non-ASCII names) | TRUE; GetLastError unchanged |
| NULL | the root of the current drive (the guest's current directory) |
| last component missing | FALSE, `ERROR_FILE_NOT_FOUND` (2) |
| a directory above the last component missing; an unmapped drive; `""` | FALSE, `ERROR_PATH_NOT_FOUND` (3) |
| a file | FALSE, `ERROR_DIRECTORY` (267) |
| a UNC path | FALSE, `ERROR_NOT_SUPPORTED` (50): no network shares here |
| a path over 32,767 UTF-16 units | FALSE, `ERROR_INVALID_NAME` (123) |

Outputs: each of the three pointers may be NULL independently, and each one
receives an 8-byte little-endian count. Available to the caller is statvfs
`f_bavail`, total is `f_blocks`, total free is `f_bfree` (all times the
fragment size). On failure nothing is written.

Basis for the error cases: Microsoft documents only that the parameter is a
directory on the volume (any directory, not only the root) and that NULL
means the current disk. For the rest the behaviour follows Wine's
implementation. Wine opens the path as a directory (`FILE_DIRECTORY_FILE`),
then queries `FileFsSizeInformation`, and maps the NT status to the Win32
error in the usual way. Wine's conformance tests on Windows expect
`ERROR_PATH_NOT_FOUND` for a missing drive and for `""` (they test the
non-Ex function). Wine bug 37408 claims that Windows accepts a file path,
but it was closed as invalid without evidence. Here a file fails with 267,
as the documentation says. None of this has been checked on real Windows;
the game's own call (a directory that exists) does not depend on it.

## Verified imports

None yet.
