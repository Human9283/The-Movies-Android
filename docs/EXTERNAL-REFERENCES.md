# External references

Research from other projects that bears on this port, and how far each
finding holds for our target: `Movies.exe` 1.1 from the Russian CD
(«Фабрика грёз»). A claim from another project is treated as evidence about
that project's binary. It applies here only after it has been checked
against ours, and the check is recorded below.

No code from these projects is in this repository. Where an idea from one of
them is used, the code here is written independently from our own analysis,
and the source of the idea is named.

## lifesupport-TheMovies (KPC LifeSupport)

Repository: <https://github.com/KillerPixelCrew/lifesupport-TheMovies>
(read at commit `2d67585`).

What it is: a Windows mod (an ASI plug-in loaded through Ultimate ASI
Loader's `dinput8.dll`) that keeps the game's videos and film export working
on modern Windows and Wine. It hooks `LoadLibraryA/W`, `GetProcAddress` and
`CoCreateInstance` in the running game. It answers `wmvcore.dll`'s two
exports and the DirectShow classes the game creates with its own objects,
backed by libmpv (playback) and FFmpeg (decoding, encoding).

### Licence

The KPC LifeSupport License v1.0, not an OSI licence and not MIT. In short:

- use, modification, derivative works, porting to other platforms and other
  versions of the game are allowed, free of charge;
- every copy and derivative must keep the copyright notice, the licence text
  and attribution, must be identifiable as a derivative, and its
  user-visible documentation must say it is based on KPC LifeSupport;
- it may not be sold or put behind any payment, subscription or donation
  gate, except as part of an authorised re-release of the game by its
  rights holder;
- bundled third-party parts keep their own licences (libmpv LGPL-2.1+,
  FFmpeg LGPL/GPL, MinHook BSD-2, Ultimate ASI Loader MIT).

So its code cannot be relicensed under this repository's MIT licence.
Adapting any of it would make that part a KPC LifeSupport derivative, with
its notice, its licence text and a statement in our documentation. It would
also add a no-sale restriction that MIT does not have. This project uses
only its **findings**, re-verified on our binary, and none of its code.

### Its target executable

Its notes concern `MoviesSE.exe`: by its name, presumably a later edition
that includes the *Stunts & Effects* expansion (the project does not say
which release it is). It publishes no file size,
timestamp or version resource for it. The comparison is therefore
structural: its published addresses, checked in our binary.

| | Our CD executable | Its executable |
|---|---|---|
| file | `Movies.exe`, 11,477,288 bytes | `MoviesSE.exe` (size not published) |
| version resource | FileVersion / ProductVersion `1.1`, fixed file version 1.0.0.0, © 2002-2005 | not published |
| PE | i386, characteristics `0x10F`, GUI subsystem, image base `0x00400000`, entry `0x00A31571`, linker 7.10, checksum 0 | image base `0x00400000` (its Ghidra base) |
| PE timestamp | `0x00564544` (1970: not a real build time) | not published |
| sections | `.text`, `.rdata`, `.data`, `_PDATA`, `.rsrc`, `.Silvana` | not published |
| delay imports | `WMVCore.DLL` (`WMCreateSyncReader`, `WMCreateProfileManager`), `DINPUT8.dll` (`DirectInput8Create`), `d3d9.dll` (`Direct3DCreate9`) | it states the same two `wmvcore.dll` exports |
| imports | advapi32 10, gdi32 3, kernel32 191, msacm32 7, oleaut32 4, shell32 2, shfolder 1, user32 40, winmm 32, ws2_32 29, ole32 9, helper.dll 1 | not published |

Its published addresses, looked up in our binary:

| Its address | Its meaning | In our binary |
|---|---|---|
| `0x009EE800` | movie player graph set-up | mid-function (not a function start) |
| `0x009ED380` | "PlayVideo" | mid-function |
| `0x009EE690` | per-frame play tick | mid-function |
| `0x00A2FFA0` | export start (`CAviSyst` slot 1) | mid-function |
| `0x00A2E980` | profile load | mid-function |
| `0x00A2F770` | profile file name | mid-function |
| `0x00A2C110` | export graph creation | mid-function |
| `0x00D76CD8` | `CAviSyst` vtable | not a vtable (data) |
| `0x00D73EA4` | FMV player vtable | zeros |
| `0x010BAB18` | `CAviSyst` global instance | not checked: its use is version-specific by its own note |

**The binaries differ.** None of its addresses can be used here. The same
classes exist in ours at other addresses, found from our own RTTI:
`CAviSyst@MV` vtable `0x00CCBCE8` (7 slots, the same count it reports),
`CMoviePlayer@MV` `0x00CC8F74`, `CMoviePlayerSys@MV` `0x00CC921C`,
`CMediaSys@MV`, `CTextureRenderer@MV` `0x00CC90EC`, `CMoviesVideoFilter@MV`
`0x00CCBBF4`, `CMoviesAudioFilter@MV` `0x00CCBC74`.

### Its findings, and what holds for our binary

| Its finding | Checked in ours | Result |
|---|---|---|
| The game's only `wmvcore.dll` imports are `WMCreateSyncReader` and `WMCreateProfileManager`, delay-loaded; it never calls `WMCreateWriter` | delay-import directory | **holds** |
| Video playback (logos, intro, menu loop) goes through DirectShow: `CoCreateInstance(CLSID_FilterGraph)` and `CLSID_WMAsfReader`, rendered into the game's own texture-renderer filter | `CoCreateInstance` call sites | **holds**: `sub_0097E190` creates `CLSID_FilterGraph` then `CLSID_WMAsfReader`. Its callers are `CMediaSys@MV` slot 8 and `CMoviePlayerSys@MV` slot 2. If the filter graph cannot be created, it releases what it has and returns false |
| `WMCreateProfileManager` is used only by film export: `CreateEmptyProfile(WMT_VER_7_0)`, then `LoadProfileByData` with the `.prx` XML | callers and the call sequence | **holds**: the only caller, `sub_009AFCB0`, is reached only from `CAviSyst@MV` slot 1 (via `sub_009B0F90`). It calls `CreateEmptyProfile(0x70000)`, releases that profile, reads the file, calls `LoadProfileByData` (slot 5) and releases the manager |
| Export builds a DirectShow capture graph with `CLSID_WMAsfWriter` (WMV) or the AVI mux, plus a WAV reader and parser for staged audio | `CoCreateInstance` call sites | **partly checked**: `CLSID_WMAsfWriter` (`sub_009B0D30`), the ACM wrapper filter (`sub_009B0C80`) and `CLSID_SystemDeviceEnum` (`sub_009AE060`, compressor enumeration) are all on `CAviSyst`'s path. The rest of its sequence was not re-traced |
| Profiles are `Data\Video\WMVProfile_<quality>{_NA}.prx` inside the PAKs: UTF-16 XML, WMV3 video and WMA2 audio, five qualities | the string `Data\Video\WMVProfile_` is in our binary | **name holds**; the profile contents were not re-extracted from our PAKs |
| `WMCreateSyncReader` is effectively never reached: the studio renders scenes in real time from `.trl` data, not from video files | callers | **holds, with a reason it does not give**: our only caller is a WMA *audio decoder* in the game's audio engine. Its vtable carries the extension tag `wma`, next to the engine's effect parameters (frequency, wet/dry, delay, make-up gain). The decoder object is registered at start-up (WinMain → `sub_00948690` → `sub_00B1CD90` → `sub_00B42050`); `WMCreateSyncReader` runs only when it opens a `.wma` file (virtual slot 11 → `sub_00B7D740` → `sub_00BAAB70`). The CD ships no `.wma` files (music is `.ogg`); the executable offers `*.wma` beside `ogg` in its file filters, so this is for players' own music |
| The in-game studio and its previews render in real time from scene data | not checked | its own README calls the studio in-game play "not exercised"; no claim is taken over |
| Several DirectShow quirks (a seeking interface used as a position interface, an event-sink call landing on `AddFilter`, `Stop`+`Run` every frame) | not checked | relevant only once DirectShow playback is implemented; to be re-verified then |

### What is not taken over

- Its approach, hooks inside a Windows process, does not apply: here every
  Windows API is already the host's own.
- Its profile manager answers unknown or unparsable input with a
  made-up default profile and `S_OK`. Several of its setters return `S_OK`
  without storing anything: a success the call did not earn.
- Its addresses, and the one place where it reads the game's memory at a
  fixed address (its export audio path).

## sp00nznet/themovies

<https://github.com/sp00nznet/themovies> (MIT): the static recompilation of
the Steam 1.2 executable that this project's toolchain approach follows
(docs/AUDIT.md). Its executable is also not ours; its function addresses are
not reused, and our catalog and lift are made from our own binary.
