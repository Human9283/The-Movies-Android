# Guest exception handling

## 1. Executable exception model

The supported executable uses the MSVC 7.x C++ exception model
(FuncInfo magic 0x19930520) and CRT SEH registration records.

Static analysis of Movies.exe 1.1 found:

| Structure | Count |
|---|---:|
| FuncInfo records | 7,482 |
| Frame-handler thunks | 7,480 |
| Registration sites with a FuncInfo thunk | 5,777 |
| Registration sites using another handler | 7 |
| Functions registering an exception frame | 5,653 |
| Functions with C++ EH frames | 5,646 |
| Functions containing a C++ throw call | 1,031 |
| Call sites to _CxxThrowException | 1,035 |

The C++ frame-handler thunk targets 0x00A2C942. The seven alternate
registration sites target 0x00A31BB4, which has an _except_handler3
prologue.

The throw routine is _CxxThrowException at 0x00A2D3D6. It builds an MSVC
exception record with code 0xE06D7363 and calls RaiseException.

## 2. Imports involved

The executable imports or references:

- RaiseException
- RtlUnwind
- SetUnhandledExceptionFilter
- UnhandledExceptionFilter
- IsBadReadPtr
- IsBadWritePtr
- IsBadCodePtr
- TerminateProcess

## 3. Lifted frame representation

The lifted C preserves guest SEH registration records. Accesses to
fs:[0] are represented through guest memory, so functions link and unlink
their records on the guest stack.

The current generated lift does not contain the exception landing pads
required to resume execution at a C++ catch block. The generated files
contain no RECOMP_EH_FRAME or RECOMP_EH_ENTER expansions.

A catch block resumes at a label inside its parent function. Correct
dispatch therefore requires both generated landing pads and a runtime
dispatcher that restores the guest frame and stack state.

## 4. Current host behavior

The project shim src/host/tm_except.c logs the exception code, flags and
arguments. It decodes delay-load failures and terminates the host run with
exit code 3. It does not attempt to continue execution after a guest
exception.

pcrecomp/win32hle's current RtlUnwind implementation does not unwind guest
frames. Its RaiseException path also terminates the run.

A guest access violation currently becomes a host signal such as SIGSEGV.
It is not dispatched through guest SEH.

## 5. Runtime observations

The measured Linux startup and graphics initialization traces do not
enter _CxxThrowException or __CxxFrameHandler.

A delay-load failure for a missing d3d9.dll produced
RaiseException(0xC06D007E). This is a missing-DLL error, not a C++ throw.
With the host D3D9 module available, the game reaches device initialization
without that exception.

The measured traces cover startup, intro and main-menu execution. They do
not establish exception-free behavior during a complete studio session,
save/load, malformed data or error handling.

## 6. Requirements for guest exception support

A correct implementation requires:

1. Regenerate the lifted C with pcrecomp's exception-handling pass enabled.
2. Port the guest exception dispatcher to the win32hle runtime.
3. Maintain dispatcher state per guest thread.
4. Walk guest exception registration records and match MSVC catch types.
5. Execute unwind and catch funclets as guest code.
6. Resume through the generated landing pad with the correct guest
   instruction, frame and stack state.
7. Test throw/catch, nested handlers, unwind funclets and SEH behavior
   with isolated guest test programs before enabling game execution.

Partial dispatch without landing pads or unwind support is not compatible
with the executable's exception model.
