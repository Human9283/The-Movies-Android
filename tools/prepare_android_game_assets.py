#!/usr/bin/env python3
"""Prepare locally owned The Movies files for the Android first-run installer."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import tarfile


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_dir", type=pathlib.Path, help="tm_install.py output directory")
    parser.add_argument(
        "--assets",
        type=pathlib.Path,
        default=pathlib.Path("android-game/app/src/main/assets"),
        help="Android app assets directory",
    )
    args = parser.parse_args()

    game_dir = args.game_dir.resolve()
    assets = args.assets.resolve()
    source_manifest = game_dir / "tm_manifest.json"
    if not source_manifest.is_file():
        raise SystemExit(f"Missing installer manifest: {source_manifest}")
    if not (game_dir / "Movies.exe").is_file() or not (game_dir / "Data").is_dir():
        raise SystemExit("The game directory must contain Movies.exe and Data/")

    original = json.loads(source_manifest.read_text(encoding="utf-8"))
    source_rows = original.get("files")
    if not isinstance(source_rows, list) or not source_rows:
        raise SystemExit("tm_manifest.json contains no file records")

    problems = original.get("problems", [])
    if not isinstance(problems, list):
        raise SystemExit("tm_manifest.json has an invalid problems field")
    if problems:
        preview = "\\n".join(f"  - {problem}" for problem in problems[:10])
        suffix = f"\\n  ... and {len(problems) - 10} more" if len(problems) > 10 else ""
        raise SystemExit(
            "The disc extraction reported problems; refusing to package an incomplete or suspect install:\\n"
            + preview + suffix
        )

    expected_rows = original.get("expected_file_rows")
    if not isinstance(expected_rows, int) or expected_rows != len(source_rows):
        raise SystemExit(
            "tm_manifest.json does not confirm that every MSI file row was extracted "
            "(regenerate it with the current tm_install.py)"
        )
    invalid_sizes = [
        item.get("path", "<unknown>")
        for item in source_rows
        if item.get("size_ok") is not True
    ]
    if invalid_sizes:
        preview = "\\n".join(f"  - {path}" for path in invalid_sizes[:10])
        suffix = f"\\n  ... and {len(invalid_sizes) - 10} more" if len(invalid_sizes) > 10 else ""
        raise SystemExit("One or more extracted files do not match the MSI size table:\\n" + preview + suffix)

    unique: dict[str, dict] = {}
    windows_names: dict[str, str] = {}
    duplicates = 0
    for item in source_rows:
        relative = item.get("path")
        if not isinstance(relative, str) or not relative or relative.startswith(("/", "\\")) or "\\" in relative:
            raise SystemExit(f"Invalid path in game manifest: {relative!r}")
        parts = relative.split("/")
        if any(part in ("", ".", "..") or "\x00" in part for part in parts):
            raise SystemExit(f"Unsafe relative game path: {relative!r}")

        size = item.get("size")
        recorded_hash = item.get("sha256")
        if not isinstance(size, int) or size < 0 or not isinstance(recorded_hash, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", recorded_hash):
            raise SystemExit(f"Missing or invalid size/SHA-256: {relative}")

        for depth in range(1, len(parts) + 1):
            prefix = "/".join(parts[:depth])
            windows_key = prefix.casefold()
            previous_name = windows_names.get(windows_key)
            if previous_name is not None and previous_name != prefix:
                raise SystemExit(
                    f"Windows case-insensitive path collision: {previous_name!r} and {prefix!r}"
                )
            windows_names[windows_key] = prefix

        previous = unique.get(relative)
        if previous is not None:
            if previous["size"] != size or previous["sha256"] != recorded_hash.lower():
                raise SystemExit(f"Conflicting duplicate source entries: {relative}")
            duplicates += 1
        else:
            unique[relative] = {"path": relative, "size": size, "sha256": recorded_hash.lower()}

    files = sorted(unique.values(), key=lambda item: item["path"].casefold())
    total_bytes = 0
    for item in files:
        path = (game_dir / pathlib.PurePosixPath(item["path"])).resolve()
        if not path.is_file() or not path.is_relative_to(game_dir):
            raise SystemExit(f"Manifest path is missing or escapes the game directory: {item['path']}")
        actual_size = path.stat().st_size
        actual_hash = sha256_file(path)
        if actual_size != item["size"] or actual_hash != item["sha256"]:
            raise SystemExit(f"Installed game file failed SHA-256 validation: {item['path']}")
        total_bytes += actual_size

    assets.mkdir(parents=True, exist_ok=True)
    manifest = {
        "files": files,
        "problems": original.get("problems", []),
        "total_bytes": total_bytes,
        "source_file_rows": len(source_rows),
        "duplicate_source_rows": duplicates,
    }
    manifest_path = assets / "game-manifest.json"
    archive_path = assets / "game-data.tar"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")

    expected = {item["path"]: item["size"] for item in files}
    with tarfile.open(archive_path, "w", format=tarfile.USTAR_FORMAT) as archive:
        seen_directories: set[str] = set()
        for item in files:
            relative = item["path"]
            encoded = relative.encode("utf-8")
            if len(encoded) > 255:
                raise SystemExit(f"USTAR path exceeds 255 UTF-8 bytes: {relative}")

            parent = pathlib.PurePosixPath(relative).parent
            parents = list(reversed(parent.parents)) + [parent] if str(parent) != "." else []
            for directory in parents:
                name = directory.as_posix()
                if name == "." or name in seen_directories:
                    continue
                seen_directories.add(name)
                info = tarfile.TarInfo(name.rstrip("/") + "/")
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                info.uid = info.gid = info.mtime = 0
                info.uname = info.gname = ""
                archive.addfile(info)

            path = game_dir / pathlib.PurePosixPath(relative)
            info = archive.gettarinfo(str(path), arcname=relative)
            info.uid = info.gid = info.mtime = 0
            info.uname = info.gname = ""
            with path.open("rb") as stream:
                archive.addfile(info, stream)

    with tarfile.open(archive_path, "r:") as archive:
        actual = {
            item.name: item.size
            for item in archive.getmembers()
            if item.isfile()
        }
    if actual != expected:
        archive_path.unlink(missing_ok=True)
        manifest_path.unlink(missing_ok=True)
        raise SystemExit("USTAR inventory differs from the verified game manifest")

    archive_bytes = archive_path.stat().st_size
    if archive_bytes >= 2 * 1024**3:
        raise SystemExit(
            "game-data.tar is 2 GiB or larger; Android APK asset packaging "
            "cannot use this single-file payload. Do not truncate it."
        )

    print(f"Verified installed files: {len(files)}")
    print(f"Identical duplicate source rows removed: {duplicates}")
    print(f"Game data bytes: {total_bytes}")
    print(f"USTAR asset bytes: {archive_bytes}")
    print(f"Manifest: {manifest_path}")
    print(f"Archive: {archive_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
