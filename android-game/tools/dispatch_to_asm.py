#!/usr/bin/env python3
"""Convert the generated C dispatch table into AArch64 ELF assembly.

The lifted function bodies remain ordinary C translation units. Only the
85k-entry address/function-pointer index is emitted as assembler data, avoiding
Clang's enormous C initializer AST while preserving recomp_types.h's ABI.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

ENTRY = re.compile(r"^\s*\{\s*0x([0-9A-Fa-f]{8})u,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\},\s*$")
COUNT = re.compile(r"const\s+uint32_t\s+recomp_dispatch_count\s*=\s*(\d+)\s*;")
ENTRY_VA = re.compile(r"const\s+uint32_t\s+tm_entry_va\s*=\s*0x([0-9A-Fa-f]{8})u\s*;")
TABLE = re.compile(r"recomp_dispatch_table\[\]\s*=\s*\{(.*?)\};", re.S)


def convert(source: pathlib.Path, destination: pathlib.Path) -> int:
    text = source.read_text(encoding="utf-8")
    table_match = TABLE.search(text)
    count_match = COUNT.search(text)
    entry_match = ENTRY_VA.search(text)
    if not (table_match and count_match and entry_match):
        raise ValueError("dispatch C is missing its table, count, or tm_entry_va definition")

    entries: list[tuple[int, str]] = []
    for line_no, line in enumerate(table_match.group(1).splitlines(), 1):
        if not line.strip():
            continue
        match = ENTRY.match(line)
        if not match:
            raise ValueError(f"unrecognized dispatch entry at table line {line_no}: {line!r}")
        entries.append((int(match.group(1), 16), match.group(2)))

    expected = int(count_match.group(1))
    if not entries or len(entries) != expected:
        raise ValueError(f"dispatch count mismatch: parsed {len(entries)}, declared {expected}")
    addresses = [address for address, _ in entries]
    if addresses != sorted(set(addresses)):
        raise ValueError("dispatch addresses must be strictly increasing and unique")

    entry_va = int(entry_match.group(1), 16)
    out = [
        "/* Generated from recomp_dispatch.c; do not edit. */",
        ".section .data.rel.ro,\\"aw\\",%progbits",
        ".p2align 3",
        ".global recomp_dispatch_table",
        ".type recomp_dispatch_table, %object",
        "recomp_dispatch_table:",
    ]
    for address, symbol in entries:
        out.extend((f"    .long 0x{address:08x}", "    .zero 4", f"    .xword {symbol}"))
    out.extend((
        f".size recomp_dispatch_table, .-recomp_dispatch_table",
        ".p2align 2",
        ".global recomp_dispatch_count",
        ".type recomp_dispatch_count, %object",
        "recomp_dispatch_count:",
        f"    .long {len(entries)}",
        ".size recomp_dispatch_count, .-recomp_dispatch_count",
        ".p2align 2",
        ".global tm_entry_va",
        ".type tm_entry_va, %object",
        "tm_entry_va:",
        f"    .long 0x{entry_va:08x}",
        ".size tm_entry_va, .-tm_entry_va",
        ".section .note.GNU-stack,\\"\\",%progbits",
        "",
    ))
    destination.write_text("\\n".join(out), encoding="utf-8", newline="\\n")
    return len(entries)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    try:
        count = convert(args.input, args.output)
    except (OSError, ValueError) as exc:
        print(f"[dispatch-asm] ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"[dispatch-asm] emitted {count:,} entries to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
