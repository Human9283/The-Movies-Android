#!/usr/bin/env python3
"""Patch the pinned pcrecomp log-seed probe to trust runtime ICALL/ITAIL evidence.

The byte-pattern probe can mistake PUSH immediates for ASCII strings. A runtime
indirect-call/tail-call target is already proven to be an executed destination,
so only the string heuristic is bypassed for those two log classes; instruction
decoding and pointer-table checks remain enabled.

Usage:
    python tools/patch_runtime_dispatch_seeds.py /path/to/pcrecomp
"""
from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count == 0 and new in source:
        return source
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one source match, found {count}")
    return source.replace(old, new, 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pcrecomp", type=Path, help="pinned pcrecomp checkout")
    root = parser.parse_args().pcrecomp
    dis_path = root / "tools/disasm/disasm32.py"
    seed_path = root / "tools/disasm/seed_from_log.py"
    if not dis_path.is_file() or not seed_path.is_file():
        raise SystemExit(f"not a pcrecomp checkout: {root}")

    dis = dis_path.read_text(encoding="utf-8")
    dis = replace_once(
        dis,
        "    def probes_as_function_body(self, va: int, window: int = 4096) -> bool:",
        "    def probes_as_function_body(self, va: int, window: int = 4096, ignore_ascii: bool = False) -> bool:",
        "disasm32 signature",
    )
    dis = replace_once(
        dis,
        "        if n >= 4 and n < len(data) and data[n] == 0 and not pushes:",
        "        if n >= 4 and n < len(data) and data[n] == 0 and not pushes and not ignore_ascii:",
        "ANSI string guard",
    )
    old_utf16 = r"        if n >= 4 and data[2 * n:2 * n + 2] == b'\0\0':"
    new_utf16 = old_utf16[:-1] + " and not ignore_ascii:"
    dis = replace_once(dis, old_utf16, new_utf16, "UTF-16 string guard")
    compile(dis, str(dis_path), "exec")
    dis_path.write_text(dis, encoding="utf-8")

    seed = seed_path.read_text(encoding="utf-8")
    seed = replace_once(
        seed,
        "        if not dis.probes_as_function_body(va):",
        '        runtime_dispatch = kind in ("indirect call", "indirect tail call")\n'
        "        if not dis.probes_as_function_body(va, ignore_ascii=runtime_dispatch):",
        "runtime seed validation",
    )
    compile(seed, str(seed_path), "exec")
    seed_path.write_text(seed, encoding="utf-8")
    print("Runtime ICALL/ITAIL seeds bypass only ASCII string heuristics.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
