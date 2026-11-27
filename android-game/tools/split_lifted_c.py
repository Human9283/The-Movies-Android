#!/usr/bin/env python3
"""Split generated pcrecomp C files into bounded AArch64 build translation units."""

from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path

FUNCTION = re.compile(r"(?m)^void\s+(sub_[A-Za-z0-9_]+)\s*\(void\)\s*\{")
NUMBERED_SOURCE = re.compile(r"^recomp_[0-9]{4}\.c$")
TOKENS = re.compile(
    r'''/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]''',
    re.S,
)


def function_spans(source: str, path: Path) -> list[tuple[int, int, str]]:
    declarations = list(FUNCTION.finditer(source))
    if not declarations:
        raise SystemExit(f"{path}: no generated sub_* functions found")

    spans: list[tuple[int, int, str]] = []
    declaration_index = 0
    active = False
    depth = 0
    start = 0
    name = ""

    for token in TOKENS.finditer(source):
        position = token.start()
        while declaration_index < len(declarations) and declarations[declaration_index].start() < position:
            declaration = declarations[declaration_index]
            if active:
                raise SystemExit(f"{path}: next function begins before the previous one closes")
            start = declaration.start()
            name = declaration.group(1)
            active = True
            depth = 0
            declaration_index += 1

        value = token.group(0)
        if not active or value not in ("{", "}"):
            continue
        if value == "{":
            depth += 1
        else:
            depth -= 1
            if depth == 0:
                spans.append((start, token.end(), name))
                active = False

    if active or declaration_index != len(declarations):
        raise SystemExit(f"{path}: incomplete generated-function parse")
    return spans


def split_source(path: Path, output: Path, max_bytes: int, source_index: int) -> tuple[int, int]:
    source = path.read_text(encoding="utf-8")
    spans = function_spans(source, path)
    header = source[:spans[0][0]].rstrip() + "\n\n"

    functions: list[str] = []
    names: list[str] = []
    for start, end, name in spans:
        names.append(name)
        functions.append(source[start:end].rstrip() + "\n")

    if len(names) != len(set(names)):
        raise SystemExit(f"{path}: duplicate function names in one translation unit")

    header_bytes = len(header.encode("utf-8"))
    chunks: list[list[str]] = []
    current: list[str] = []
    current_bytes = header_bytes

    for function in functions:
        function_bytes = len(function.encode("utf-8")) + 1
        if current and current_bytes + function_bytes > max_bytes:
            chunks.append(current)
            current = []
            current_bytes = header_bytes
        if current_bytes + function_bytes > max_bytes:
            raise SystemExit(f"{path}: a single function exceeds the {max_bytes}-byte limit")
        current.append(function)
        current_bytes += function_bytes

    if current:
        chunks.append(current)

    largest = 0
    for chunk_index, chunk in enumerate(chunks):
        destination = output / f"recomp_split_{source_index:04d}_{chunk_index:03d}.c"
        text = header + "\n".join(chunk)
        destination.write_text(text, encoding="utf-8", newline="\n")
        largest = max(largest, destination.stat().st_size)

    return len(chunks), largest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="run_lift.py output directory")
    parser.add_argument("output", type=Path, help="Android-only generated-source directory")
    parser.add_argument("--max-bytes", type=int, default=3 * 1024 * 1024)
    args = parser.parse_args()

    source = args.source.resolve()
    output = args.output.resolve()
    if args.max_bytes < 4096:
        raise SystemExit("--max-bytes must be at least 4096")
    if not source.is_dir():
        raise SystemExit(f"Generated source directory does not exist: {source}")

    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    # Keep dispatch C, headers and auxiliary generated files. Only split the
    # numbered recomp_NNNN.c files that contain lifted guest functions.
    for item in source.iterdir():
        if item.is_file() and not NUMBERED_SOURCE.fullmatch(item.name):
            shutil.copy2(item, output / item.name)

    numbered = sorted(
        item for item in source.iterdir()
        if item.is_file() and NUMBERED_SOURCE.fullmatch(item.name)
    )
    if not numbered:
        raise SystemExit(f"No recomp_NNNN.c files found in {source}")

    function_count = 0
    output_count = 0
    largest = 0
    seen: set[str] = set()

    for source_index, item in enumerate(numbered):
        spans = function_spans(item.read_text(encoding="utf-8"), item)
        names = {name for _, _, name in spans}
        if len(names) != len(spans):
            raise SystemExit(f"{item}: duplicate guest functions")
        duplicate = seen.intersection(names)
        if duplicate:
            raise SystemExit(f"Duplicate lifted symbols across files: {sorted(duplicate)[:8]}")
        seen.update(names)
        function_count += len(names)
        chunks, size = split_source(item, output, args.max_bytes, source_index)
        output_count += chunks
        largest = max(largest, size)

    if not (output / "recomp_dispatch.S").is_file():
        raise SystemExit("recomp_dispatch.S is missing; run dispatch_to_asm.py first")
    if not (output / "recomp_funcs.h").is_file():
        raise SystemExit("recomp_funcs.h is missing from the lift output")

    emitted = sorted(output.glob("recomp_split_*.c"))
    if len(emitted) != output_count or any(file.stat().st_size > args.max_bytes for file in emitted):
        raise SystemExit("Generated translation-unit size/count verification failed")

    print(f"Input files: {len(numbered)}")
    print(f"Lifted functions preserved: {function_count}")
    print(f"Android translation units: {len(emitted)}")
    print(f"Largest unit: {largest} bytes (limit {args.max_bytes})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
