#!/usr/bin/env python3
"""Seed the function catalog with the MSVC CRT's initializer tables.

`_cinit` calls every non-null pointer in two tables before WinMain:
`__xi_a..__xi_z` (C initializers, through `_initterm_e`) and
`__xc_a..__xc_z` (C++ dynamic initializers: global constructors, through
`_initterm`). The pointers live in data, so a disassembler can miss one; a
missed initializer becomes an unresolved indirect call and that global is
never constructed (docs/M2.md).

    python3 tools/crt_init_seeds.py Movies.exe --merge rtti_seeds.json -o seeds.json

The table bounds are found by matching `_cinit`'s code from the PE entry
point (`mov ecx/esi, __x?_a; mov edi, __x?_z` before a pointer-calling loop);
--table LO:HI gives them by hand instead. The output is a seed list in the
same format as rtti.py's, for disasm32.py --seed-functions.
"""
import argparse
import json
import struct
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs


def find_tables(pe, img, base, max_funcs=64):
    """Follow direct calls from the entry point (breadth first, bounded) and
    collect [lo, hi) pairs from `mov r1, imm; mov r2, imm` where both are in
    the image's data and lo < hi: the shape of _cinit's two table walks."""
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    entry = base + pe.OPTIONAL_HEADER.AddressOfEntryPoint
    lo_img, hi_img = base, base + pe.OPTIONAL_HEADER.SizeOfImage
    todo, seen, tables = [entry], set(), set()
    while todo and len(seen) < max_funcs:
        f = todo.pop(0)
        if f in seen or not (lo_img <= f < hi_img):
            continue
        seen.add(f)
        movs = []
        for ins in md.disasm(img[f - base:f - base + 0x200], f):
            if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
                todo.append(int(ins.op_str, 16))
            # _cinit loads both bounds within a few instructions, with
            # register moves between (`mov esi, lo; mov eax, esi; mov edi, hi`);
            # only control flow ends a candidate pair.
            if ins.mnemonic == 'mov' and ', 0x' in ins.op_str:
                reg, imm = ins.op_str.split(', ')
                v = int(imm, 16)
                if lo_img <= v < hi_img and reg in ('eax', 'ecx', 'edx', 'ebx', 'esi', 'edi'):
                    movs.append(v)
                    if len(movs) >= 2:
                        lo, hi = movs[-2], movs[-1]
                        if lo < hi and (hi - lo) % 4 == 0 and hi - lo < 0x10000:
                            tables.add((lo, hi))
            elif ins.mnemonic.startswith('j') or ins.mnemonic in ('call', 'ret'):
                movs = []
            if ins.mnemonic in ('ret', 'int3'):
                break
    return sorted(tables)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--merge', help='another seed list to include (rtti_seeds.json)')
    ap.add_argument('--table', action='append', default=[], help='LO:HI, hex, instead of searching')
    a = ap.parse_args(argv)

    pe = pefile.PE(a.exe)
    img = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    text = [s for s in pe.sections if s.Characteristics & 0x20000000]
    code_ranges = [(base + s.VirtualAddress, base + s.VirtualAddress + s.Misc_VirtualSize) for s in text]

    tables = ([tuple(int(x, 16) for x in t.split(':')) for t in a.table] if a.table
              else find_tables(pe, img, base))
    seeds, n_tables = [], 0
    for lo, hi in tables:
        ptrs = [struct.unpack_from('<I', img, va - base)[0] for va in range(lo, hi, 4)]
        nz = [p for p in ptrs if p]
        # an initializer table holds only null or code pointers
        if not nz or not all(any(c0 <= p < c1 for c0, c1 in code_ranges) for p in nz):
            continue
        n_tables += 1
        print('[*] table 0x%08X-0x%08X: %d slots, %d initializers' % (lo, hi, len(ptrs), len(nz)))
        seeds += [{'address': p, 'address_hex': '0x%08X' % p, 'note': 'CRT initializer table 0x%08X' % lo}
                  for p in nz]
    if not n_tables:
        sys.exit('no initializer table found; give --table LO:HI')

    out = seeds
    if a.merge:
        have = {s['address'] for s in seeds}
        out = seeds + [s for s in json.load(open(a.merge)) if s['address'] not in have]
    json.dump(out, open(a.output, 'w'), indent=1)
    print('[*] %d initializer seeds, %d seeds written to %s' % (len(seeds), len(out), a.output))
    return 0


if __name__ == '__main__':
    sys.exit(main())
