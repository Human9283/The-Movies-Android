#!/usr/bin/env python3
"""Exception-handling structures in Movies.exe, for docs/EXCEPTIONS.md.

    python3 -I tools/eh_scan.py GAME/Movies.exe FUNCTIONS.json [SEEN.txt] > eh.json

FUNCTIONS.json is the catalog (pcrecomp's functions.json). SEEN.txt, if
given, is tm_host --seen-out: the code addresses a run entered; the report
then says which of the structures found were reached.

Static, from the bytes of the executable:
  * MSVC C++ EH FuncInfo records, by their magic (0x1993052x);
  * frame-handler thunks: `mov eax, FuncInfo; jmp handler`, and the handler
    they share (the CRT's __CxxFrameHandler);
  * SEH registration sequences: `mov eax, fs:[0]` / `mov fs:[0], reg` and
    `push handler` right before `mov eax, fs:[0]`;
  * the throw helper: the function that raises 0xE06D7363 (MSVC's C++
    exception code) through RaiseException, i.e. _CxxThrowException, and the
    calls to it;
  * the imports that take part (RaiseException, RtlUnwind, ...).
Prints JSON with counts and addresses; no bytes of the game.
"""
import bisect
import json
import struct
import sys

import pefile


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    pe = pefile.PE(argv[1])
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    cat = json.load(open(argv[2]))
    starts = sorted(f['address'] for f in cat['functions'])
    seen = set()
    if len(argv) == 4:
        seen = {int(l, 16) for l in open(argv[3]) if l.strip()}

    def func_of(va):
        i = bisect.bisect_right(starts, va) - 1
        return starts[i] if i >= 0 else None

    text = [s for s in pe.sections if s.Characteristics & 0x20000000]   # executable
    data = [s for s in pe.sections if not s.Characteristics & 0x20000000]

    def ranges(secs):
        for s in secs:
            lo = s.VirtualAddress
            yield base + lo, img[lo:lo + max(s.Misc_VirtualSize, s.SizeOfRawData)]

    def in_data(va):
        return any(lo <= va < lo + len(b) for lo, b in ranges(data))

    def in_text(va):
        return any(lo <= va < lo + len(b) for lo, b in ranges(text))

    def u32(va):
        o = va - base
        return struct.unpack_from('<I', img, o)[0] if 0 <= o <= len(img) - 4 else None

    iat = {}
    for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        for i in e.imports:
            if i.name:
                iat[i.address] = (e.dll.decode(), i.name.decode())
    eh_imports = sorted({n for _, n in iat.values() if n in (
        'RaiseException', 'RtlUnwind', 'UnhandledExceptionFilter', 'SetUnhandledExceptionFilter',
        'IsBadReadPtr', 'IsBadWritePtr', 'IsBadCodePtr', 'TerminateProcess')})

    # FuncInfo magics
    magic = {}
    for lo, b in ranges(data):
        for m in (0x19930520, 0x19930521, 0x19930522):
            key = struct.pack('<I', m)
            o = b.find(key)
            while o >= 0:
                if o % 4 == 0:
                    magic.setdefault(hex(m), []).append(lo + o)
                o = b.find(key, o + 1)
    funcinfo = {va for v in magic.values() for va in v}

    # thunks mov eax, imm32 (B8) ; jmp rel32 (E9)
    thunks, handlers = {}, {}
    seh_load = seh_store = 0
    seh_handlers = {}
    for lo, b in ranges(text):
        o = b.find(b'\xB8')
        while 0 <= o < len(b) - 10:
            if b[o + 5] == 0xE9:
                imm = struct.unpack_from('<I', b, o + 1)[0]
                if imm in funcinfo:
                    tgt = (lo + o + 10 + struct.unpack_from('<i', b, o + 6)[0]) & 0xFFFFFFFF
                    thunks[lo + o] = (imm, tgt)
                    handlers[tgt] = handlers.get(tgt, 0) + 1
            o = b.find(b'\xB8', o + 1)
        for pat in (b'\x64\xA1\x00\x00\x00\x00',):
            o = b.find(pat)
            while o >= 0:
                seh_load += 1
                if o >= 5 and b[o - 5] == 0x68:                 # push handler; mov eax, fs:[0]
                    h = struct.unpack_from('<I', b, o - 4)[0]
                    if not in_text(h):                       # an immediate, not a handler
                        o = b.find(pat, o + 1)
                        continue
                    kind = 'C++ (FuncInfo thunk)' if h in thunks else 'other'
                    seh_handlers.setdefault(h, [kind, 0, set()])
                    seh_handlers[h][1] += 1
                    seh_handlers[h][2].add(func_of(lo + o))
                o = b.find(pat, o + 1)
        for pat in (b'\x64\x89\x25\x00\x00\x00\x00', b'\x64\x89\x0D\x00\x00\x00\x00', b'\x64\xA3\x00\x00\x00\x00',
                    b'\x64\x89\x15\x00\x00\x00\x00', b'\x64\x89\x1D\x00\x00\x00\x00'):
            o = b.find(pat)
            while o >= 0:
                seh_store += 1
                o = b.find(pat, o + 1)

    # the throw helper (_CxxThrowException): a function that calls
    # [RaiseException] with the code 0xE06D7363, held as an immediate or in a
    # template record in data that it copies (mov esi, template; rep movsd)
    raise_iat = [a for a, (_, n) in iat.items() if n == 'RaiseException']
    throwers = set()
    for lo, b in ranges(text):
        for a in raise_iat:
            k = b'\xFF\x15' + struct.pack('<I', a)
            o = b.find(k)
            while o >= 0:
                f = func_of(lo + o)
                body = b[f - lo:lo + o - lo] if f is not None and f >= lo else b''
                hit = b'\x63\x73\x6D\xE0' in body
                p_ = body.find(b'\xBE')                          # mov esi, imm32
                while not hit and p_ >= 0 and p_ + 5 <= len(body):
                    t = struct.unpack_from('<I', body, p_ + 1)[0]
                    hit = in_data(t) and u32(t) == 0xE06D7363
                    p_ = body.find(b'\xBE', p_ + 1)
                if hit and f is not None:
                    throwers.add(f)
                o = b.find(k, o + 1)
    throw_calls = {t: [] for t in throwers}
    for lo, b in ranges(text):
        o = b.find(b'\xE8')
        while 0 <= o < len(b) - 5:
            tgt = (lo + o + 5 + struct.unpack_from('<i', b, o + 1)[0]) & 0xFFFFFFFF
            if tgt in throw_calls:
                throw_calls[tgt].append(lo + o)
            o = b.find(b'\xE8', o + 1)

    seh_funcs = set()
    for h, (kind, n, fs) in seh_handlers.items():
        seh_funcs |= {f for f in fs if f is not None}
    cxx_funcs = {f for h, (kind, n, fs) in seh_handlers.items() if kind.startswith('C++') for f in fs if f is not None}
    out = {
        'image_base': hex(base),
        'eh_imports': eh_imports,
        'funcinfo_magic': {k: len(v) for k, v in magic.items()},
        'frame_handler_thunks': len(thunks),
        'frame_handlers': {hex(t): n for t, n in sorted(handlers.items(), key=lambda x: -x[1])},
        'fs0_loads': seh_load,
        'fs0_stores': seh_store,
        'seh_registrations_by_handler_kind': {
            k: sum(n for kk, n, _ in seh_handlers.values() if kk == k) for k in ('C++ (FuncInfo thunk)', 'other')},
        'other_seh_handlers': sorted(hex(h) for h, (k, n, _) in seh_handlers.items() if k == 'other'),
        'functions_registering_a_frame': len(seh_funcs),
        'functions_with_cxx_eh_frames': len(cxx_funcs),
        'throw_helpers': sorted(hex(t) for t in throwers if t is not None),
        'throw_call_sites': {hex(t): len(v) for t, v in throw_calls.items() if t is not None},
        'functions_that_throw': len({func_of(c) for v in throw_calls.values() for c in v}),
    }
    if seen:
        reached_throwers = sorted({func_of(c) for v in throw_calls.values() for c in v} & seen)
        out['run'] = {
            'functions_entered': len(seen),
            'throw_helper_entered': sorted(hex(t) for t in throwers if t in seen),
            'frame_handlers_entered': sorted(hex(t) for t in handlers if t in seen),
            'other_seh_handlers_entered': sorted(hex(h) for h, (k, n, _) in seh_handlers.items() if k == 'other' and h in seen),
            'functions_registering_a_frame_entered': len(seh_funcs & seen),
            'functions_with_cxx_eh_frames_entered': len(cxx_funcs & seen),
            'functions_containing_a_throw_entered': len(reached_throwers),
            'functions_containing_a_throw_entered_list': [hex(f) for f in reached_throwers[:50]],
        }
    json.dump(out, sys.stdout, indent=1)
    print()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
