# Exceptions in Movies.exe 1.1 (blocker B4)

This is an evidence report. It covers what the executable contains, what a
run reaches, and what a working implementation would need. Nothing in
`src/` implements guest exception dispatch, and this report adds none.

Tags: [static] read from the bytes of `Movies.exe` (SHA-1
`(hash removed)`); [run] measured in a run of the host; [ref] read from
pcrecomp's source; [hyp.] not verified.

## How the numbers were obtained

```bash
cd ~/tm/game && ~/tm/build/tm_host ~/tm/game --user "$(mktemp -d)" --seen-out seen.txt
```

```bash
python3 -I tools/eh_scan.py ~/tm/game/Movies.exe ~/tm/work/functions.json seen.txt > eh.json
```

- `--seen-out` writes every function entry address the run executed, one
  per line. It is written on exit, on a crash and at `--stop-at-winmain`.
- The run is the M3 boundary scenario (`tests/m3_boundary.py`). It ends at
  the Windows Media check's warning box and the SIGSEGV on the exit path
  that follows (IMPORTS.md, "The current boundary"). 1,640 distinct
  functions were entered. A `--stop-at-winmain` run enters 1,245.
- `eh_scan.py` prints counts and addresses only, no bytes of the game.

## Findings

### 1. Which exception model does the game use? [static]

MSVC C++ exception handling (the `0x19930520` FuncInfo layout, as built by
Visual C++ 7.x), plus a few CRT `__try` frames:

| What | Count |
|---|---|
| FuncInfo records (magic `0x19930520`; none with `…21` or `…22`) | 7,482 |
| frame-handler thunks `mov eax, FuncInfo; jmp handler` | 7,480, all to one handler, `0x00A2C942` (`__CxxFrameHandler`) |
| `push handler; mov eax, fs:[0]` registrations with a FuncInfo thunk | 5,777 |
| the same with another handler | 7, all `0x00A31BB4`, which has the `_except_handler3` prologue |
| functions that register a frame | 5,653 (5,646 with a C++ EH frame) |
| `mov eax, fs:[0]` loads / stores to `fs:[0]` | 7,478 / 16,184 |

One FuncInfo record has a thunk the scan does not find (7,482 vs 7,480).
That can be a record reached another way, or data that happens to match. It
was not followed up.

### 2. Does the game throw? [static]

Yes. `_CxxThrowException` is `0x00A2D3D6`. It is found as the function that
copies an `EXCEPTION_RECORD` template at `0x00CD0814` (code `0xE06D7363`)
with `rep movsd` and calls `[RaiseException]`. There are 1,035 call sites
to it, in 1,031 functions.

### 3. Which imports take part? [static]

`RaiseException`, `RtlUnwind`, `SetUnhandledExceptionFilter`,
`UnhandledExceptionFilter`, `IsBadReadPtr`, `IsBadWritePtr`,
`IsBadCodePtr`, `TerminateProcess`.

### 4. Does the start-up path throw? [run]

Not in the measured run, as far as it goes:

| | Entered |
|---|---|
| `_CxxThrowException` | no |
| `__CxxFrameHandler` (`0x00A2C942`) | no |
| `_except_handler3` (`0x00A31BB4`) | no |
| `RaiseException` (import trace) | 0 calls |
| `SetUnhandledExceptionFilter` | 1 call, from the CRT |
| functions with a C++ EH frame | 186 of 5,646 |
| functions that contain a throw call | 5: `0x0046D940`, `0x0048A9C0`, `0x00540DC0`, `0x009FBEC0`, `0x009FDD70` |

The five functions with a throw call ran, but none of their throws was
taken. The run stops before the main window, so this says nothing about
the game after start-up.

Since the DirectShow probe passes (DIRECTSHOW-STARTUP.md 9), the run goes
further and ends with one `RaiseException`: `0xC06D007E`, the delay-load
helper's error for `d3d9.dll`, which was missing then. That is an SEH
exception raised because a DLL is absent, not a C++ throw, and on Windows
it does not happen when the DLL is present. No `_CxxThrowException` was
entered. With `d3d9.dll` a host DLL, the run now reaches `CreateDevice`
with no `RaiseException` at all. With a device (`--d3d9 dxvk`), the run
through the intro and the main menu (about 60 seconds,
`docs/traces/dxvk_run_imports.json`) calls no `RaiseException` either; the
game proper has not been measured for it. The decision below stands.

### 5. What does the lifted code do with the frames? [static, lifted C]

It keeps them. The lift turns `fs:[0]` accesses into `MEM32(FS_BASE + 0)`,
so each function links and unlinks its registration record on the guest
stack exactly as the original does. The chain is in guest memory and can be
walked.

What the lift does not have is a place to resume. A C++ catch block ends by
returning an address inside its parent function, and execution continues
there with the frame's `ebp` and saved `esp`. In the lifted C that address
must be a label the function can jump to, entered through a landing pad.
`grep RECOMP_EH_FRAME` and `grep RECOMP_EH_ENTER` find no use of either in
`gen/*.c`: the current lift was made without pcrecomp's EH pass.

### 6. What happens today when the game throws? [run, source]

The game's `RaiseException` import is our `tm:RaiseException`
(`src/host/tm_except.c`). It prints the code, the flags and the
arguments (and decodes delay-load failures), then ends the run with exit
code 3. For a C++ throw the arguments are the FuncInfo magic, the thrown
object and its `ThrowInfo`, so the thrown type can be read from the log
by hand; the shim does not name it yet. win32hle's own
`RaiseException` also ends the run; its `RtlUnwind` returns without
unwinding, and `UnhandledExceptionFilter` answers
`EXCEPTION_CONTINUE_SEARCH`. A throw therefore always stops the run with its code
logged. It never continues in a wrong state.

### 7. What about hardware faults? [run]

A guest access violation is a host SIGSEGV. No `__try` frame and no
unhandled-exception filter sees it. The one fault the current run reaches
(a read of `0x00000004` in `sub_00936350`, on the exit path after the
warning box) ends the run. Whether that code relies on SEH on Windows has
not been checked. [hyp.] On Windows it would most likely end the process
too, because no `__try` frame was entered.

### 8. What would a working implementation need? [ref]

pcrecomp already has the whole design, for its native 32-bit Windows host:

- `tools/lift/eh32.py`: from the same FuncInfo, TryBlockMap and
  HandlerType tables (and from `_except_handler3` scope tables), it finds
  each frame's resume points. The lifter makes them labels in the
  function's local dispatch and emits a landing pad
  (`RECOMP_EH_FRAME` / `RECOMP_EH_ENTER`, `runtime/recomp32/recomp_types.h`).
- `runtime/native32/eh32.c` and `seh32.c`: a dispatcher that walks the
  guest `fs:[0]` chain from guest data. It matches the thrown type against
  the catch's type descriptor, runs unwind funclets and the catch funclet
  as guest code, and `longjmp`s into the parent's landing pad with the
  resume address.

For this project that means:

1. a re-lift with the EH pass turned on (a new `gen/`, rebuilt);
2. the dispatcher ported to win32hle. Its logic reads guest memory only.
   Its thread-local state and headers are Windows-specific today. It must
   keep a `jmp_buf` per guest thread and work under the machine lock;
3. `RaiseException`, `RtlUnwind` and `_except_handler3`'s callers
   connected to it;
4. tests with a small guest program that throws and catches, before the
   game uses it.

A partial version (dispatch without landing pads, or catch without unwind)
would leave guest state wrong after a throw. That is worse than stopping,
so none is added now.

### 9. How risky is it to wait? [assessment]

Low for M3. No throw is reached at start-up, and a throw stops the run with
its code and arguments logged, so the first one will be found at once. With 1,031
throwing functions, exceptions on error paths (a missing file, a bad save)
are likely later. [hyp.] Whether any is on a normal path is unknown until
the run gets further.

### 10. What would show the first need early? [plan]

- `tm:RaiseException` already prints the exception code and arguments.
  Naming the thrown type (`ThrowInfo` → `CatchableTypeArray` →
  `TypeDescriptor` name) is a small diagnostic addition worth making
  before the run reaches its first throw.
- Rerun `eh_scan.py` with each new `--seen-out` run: the
  `functions_containing_a_throw_entered` list says how close the run gets.
- `IsBad*Ptr` are win32hle's (a pointer below `0x10000` is bad, and
  nothing else is). If the game uses them to probe memory, they need the
  reserved and committed state from the VM map instead. That is not needed
  so far.

## Decision

No EH runtime now. It becomes a milestone item (docs/REPOSITORY-AUDIT.md,
"Next milestones") when the first `RaiseException` is reached on a path
the game is expected to survive, or before M6 at the latest.
