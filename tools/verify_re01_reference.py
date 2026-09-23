#!/usr/bin/env python3
"""Verify the user-supplied ARM reference without executing or distributing it.

Uses only Python's standard library. Symbol/byte fingerprints establish the
reference identity; they do not establish gameplay equivalence by themselves.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "docs/references/re01-original-fingerprints.json"


def inspect_elf(path: Path, names: list[str]) -> dict:
    data = path.read_bytes()
    if len(data) < 52:
        raise ValueError("Reference is too small for an ELF32 header")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    ident, machine = header[0], header[2]
    if ident[:7] != b"\x7fELF\x01\x01\x01" or machine != 40:
        raise ValueError("Reference must be a little-endian ELF32 ARM image")
    shoff, shentsize, shnum = header[6], header[11], header[12]
    if shentsize < 40 or shnum == 0 or shoff + shentsize * shnum > len(data):
        raise ValueError("Invalid or unsupported ELF section table")
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize)
                for i in range(shnum)]

    def section_bytes(section: tuple) -> bytes:
        offset, size = section[4], section[5]
        if offset + size > len(data):
            raise ValueError("Section extends past reference image")
        return data[offset:offset + size]

    wanted = set(names)
    found: dict[str, dict] = {}
    for section in sections:
        # SHT_DYNSYM only: retained exported symbols from the supplied image.
        if section[1] != 11:
            continue
        if section[6] >= len(sections) or section[9] < 16:
            raise ValueError("Invalid ELF dynamic symbol table")
        strings = section_bytes(sections[section[6]])
        symbols = section_bytes(section)
        for offset in range(0, len(symbols) - 15, section[9]):
            name_offset, value, size, info, _, index = struct.unpack_from(
                "<IIIBBH", symbols, offset)
            if name_offset >= len(strings):
                raise ValueError("Invalid symbol-name offset")
            end = strings.find(b"\0", name_offset)
            if end < 0:
                raise ValueError("Unterminated symbol name")
            name = strings[name_offset:end].decode("utf-8", errors="strict")
            if name not in wanted:
                continue
            if info & 0xf != 2 or index == 0 or index >= len(sections):
                raise ValueError(f"Reference symbol is not a defined function: {name}")
            target = sections[index]
            address = value & ~1  # Clear Thumb-state bit, not the Ghidra rebase.
            relative = address - target[3]
            if relative < 0 or size <= 0 or relative + size > target[5]:
                raise ValueError(f"Function has invalid byte extent: {name}")
            raw = section_bytes(target)[relative:relative + size]
            found[name] = {
                "name": name,
                "elf_address": f"0x{address:08x}",
                "thumb": bool(value & 1),
                "size": size,
                "sha256": hashlib.sha256(raw).hexdigest(),
            }
    missing = wanted - found.keys()
    if missing:
        raise ValueError("Missing symbols: " + ", ".join(sorted(missing)))
    return {
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "machine": "ELF32 little-endian ARM",
        "symbols": [found[name] for name in names],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", nargs="?", type=Path,
                        default=ROOT / "game/original/libspiderman.so")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=Path,
                        help="Also save the verified identity as JSON")
    args = parser.parse_args()
    try:
        expected = json.loads(args.manifest.read_text(encoding="utf-8"))
        actual = inspect_elf(args.reference, [s["name"] for s in expected["symbols"]])
        for key in ("size", "sha256", "machine", "symbols"):
            if actual[key] != expected[key]:
                raise ValueError(f"Reference mismatch in {key}; do not reuse RE01 addresses")
        actual["verified"] = True
        actual["ghidra_rebase_offset"] = expected["ghidra_rebase_offset"]
        actual["note"] = "Identity verification only; no ARM code was executed."
        text = json.dumps(actual, indent=2) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text, encoding="utf-8")
        print(text, end="")
        return 0
    except (OSError, ValueError, KeyError, struct.error) as error:
        print(f"Reference verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
