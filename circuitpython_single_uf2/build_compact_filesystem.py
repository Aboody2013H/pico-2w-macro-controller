#!/usr/bin/env python3
"""Create a compact, writable FAT image for the combined UF2."""

from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath

from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS


COMPACT_FILESYSTEM_SIZE = 0x7F000  # 508 KiB flash range: 0x181000..0x200000


def add_tree(fs: PyFatFS, source: Path, destination: PurePosixPath) -> None:
    for item in sorted(source.iterdir(), key=lambda entry: entry.name.lower()):
        target = destination / item.name
        target_text = str(target)
        if item.is_dir():
            fs.makedir(target_text, recreate=True)
            add_tree(fs, item, target)
        elif item.is_file():
            with item.open("rb") as source_file:
                fs.upload(target_text, source_file)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("merged_code", type=Path)
    parser.add_argument("merged_index", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--label",
        default="CIRCUITPY",
        help="FAT volume label shown by the host (maximum 11 characters)",
    )
    parser.add_argument(
        "--boot-py",
        type=Path,
        help="optional boot.py to include in the filesystem image",
    )
    args = parser.parse_args()

    volume_label = args.label.strip().upper()
    if not volume_label or len(volume_label) > 11:
        parser.error("--label must contain 1 to 11 characters")
    if args.boot_py is not None and not args.boot_py.is_file():
        parser.error("--boot-py must name an existing file")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        args.output.unlink()
    with args.output.open("wb") as image_file:
        image_file.truncate(COMPACT_FILESYSTEM_SIZE)

    fat = PyFat()
    fat.mkfs(
        str(args.output),
        fat_type=PyFat.FAT_TYPE_FAT12,
        size=COMPACT_FILESYSTEM_SIZE,
        sector_size=512,
        number_of_fats=1,
        label=volume_label,
    )
    fat.close()

    fs = PyFatFS(str(args.output), preserve_case=True)
    try:
        for directory in ("lib", "sd"):
            source_directory = args.snapshot / directory
            if source_directory.exists():
                fs.makedir(f"/{directory}", recreate=True)
                add_tree(fs, source_directory, PurePosixPath(f"/{directory}"))

        files = {
            "code.py": args.merged_code,
            "index.html": args.merged_index,
            "builder.html": args.snapshot / "builder.html",
            "settings.toml": args.snapshot / "settings.toml",
            "settings.toml.example": args.snapshot / "settings.toml.example",
            "README.txt": args.snapshot / "README.txt",
        }
        if args.boot_py is not None:
            files["boot.py"] = args.boot_py
        for destination, source in files.items():
            if source.exists():
                with source.open("rb") as source_file:
                    fs.upload(f"/{destination}", source_file)
    finally:
        fs.close()

    print(
        f"Wrote compact {volume_label} image: {args.output} "
        f"({args.output.stat().st_size} bytes)"
    )


if __name__ == "__main__":
    main()
