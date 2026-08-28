#!/usr/bin/env python3
"""Combine Pico 2 W CircuitPython firmware and a CIRCUITPY FAT image.

The generated UF2 writes both regions in one BOOTSEL copy operation:
  * CircuitPython firmware at the addresses already present in the base UF2.
  * The writable CIRCUITPY filesystem beginning at 0x10181000.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_BLOCK_SIZE = 512
UF2_PAYLOAD_SIZE = 256
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
RP2350_ARM_S_FAMILY_ID = 0xE48BFF59
PICO2W_FILESYSTEM_ADDRESS = 0x10181000
PICO2W_FLASH_END = 0x10400000


def parse_firmware_blocks(data: bytes) -> list[bytearray]:
    if len(data) % UF2_BLOCK_SIZE:
        raise ValueError("Base UF2 size is not a multiple of 512 bytes")

    blocks: list[bytearray] = []
    for offset in range(0, len(data), UF2_BLOCK_SIZE):
        block = bytearray(data[offset : offset + UF2_BLOCK_SIZE])
        magic0, magic1, flags, _address, payload_size = struct.unpack_from(
            "<IIIII", block, 0
        )
        magic_end = struct.unpack_from("<I", block, 508)[0]
        family_id = struct.unpack_from("<I", block, 28)[0]
        if (magic0, magic1, magic_end) != (
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            UF2_MAGIC_END,
        ):
            raise ValueError(f"Invalid UF2 block at byte offset {offset}")
        if payload_size != UF2_PAYLOAD_SIZE:
            raise ValueError(f"Unexpected payload size {payload_size}")
        if not flags & UF2_FLAG_FAMILY_ID_PRESENT:
            raise ValueError("Base UF2 does not declare a family ID")
        if family_id != RP2350_ARM_S_FAMILY_ID:
            raise ValueError(f"Unexpected UF2 family ID 0x{family_id:08X}")
        blocks.append(block)
    return blocks


def make_filesystem_blocks(image: bytes) -> list[bytearray]:
    maximum_size = PICO2W_FLASH_END - PICO2W_FILESYSTEM_ADDRESS
    if not image or len(image) > maximum_size:
        raise ValueError(
            f"Filesystem image is {len(image)} bytes; maximum is {maximum_size}"
        )
    if len(image) % UF2_PAYLOAD_SIZE:
        raise ValueError("Filesystem image is not 256-byte aligned")

    blocks: list[bytearray] = []
    for offset in range(0, len(image), UF2_PAYLOAD_SIZE):
        block = bytearray(UF2_BLOCK_SIZE)
        struct.pack_into(
            "<IIIIIIII",
            block,
            0,
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            UF2_FLAG_FAMILY_ID_PRESENT,
            PICO2W_FILESYSTEM_ADDRESS + offset,
            UF2_PAYLOAD_SIZE,
            0,
            0,
            RP2350_ARM_S_FAMILY_ID,
        )
        block[32 : 32 + UF2_PAYLOAD_SIZE] = image[
            offset : offset + UF2_PAYLOAD_SIZE
        ]
        struct.pack_into("<I", block, 508, UF2_MAGIC_END)
        blocks.append(block)
    return blocks


def trim_zero_tail(image: bytes) -> bytes:
    """Drop unused 256-byte sectors at the end of a freshly built FAT image."""
    last_used = -1
    for offset in range(0, len(image), UF2_PAYLOAD_SIZE):
        if any(image[offset : offset + UF2_PAYLOAD_SIZE]):
            last_used = offset
    if last_used < 0:
        raise ValueError("Filesystem image contains no data")
    return image[: last_used + UF2_PAYLOAD_SIZE]


def renumber(blocks: list[bytearray]) -> None:
    total = len(blocks)
    for number, block in enumerate(blocks):
        struct.pack_into("<II", block, 20, number, total)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("base_uf2", type=Path)
    parser.add_argument("filesystem_image", type=Path)
    parser.add_argument("output_uf2", type=Path)
    parser.add_argument(
        "--trim-zero-tail",
        action="store_true",
        help="omit unused zero-filled filesystem sectors at the end of the UF2",
    )
    args = parser.parse_args()

    firmware_blocks = parse_firmware_blocks(args.base_uf2.read_bytes())
    filesystem_image = args.filesystem_image.read_bytes()
    original_size = len(filesystem_image)
    if args.trim_zero_tail:
        filesystem_image = trim_zero_tail(filesystem_image)
        print(
            f"Trimmed filesystem payload from {original_size} to "
            f"{len(filesystem_image)} bytes."
        )
    filesystem_blocks = make_filesystem_blocks(filesystem_image)
    blocks = firmware_blocks + filesystem_blocks
    renumber(blocks)

    args.output_uf2.parent.mkdir(parents=True, exist_ok=True)
    args.output_uf2.write_bytes(b"".join(blocks))
    print(
        f"Wrote {args.output_uf2} with {len(firmware_blocks)} firmware blocks "
        f"and {len(filesystem_blocks)} filesystem blocks ({len(blocks)} total)."
    )


if __name__ == "__main__":
    main()
