#!/usr/bin/env python3
"""Verify RE02's supplied original reference identity, without executing ARM."""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

# Shared ELF32 parser shipped with the cumulative RE01 source.
from verify_re01_reference import inspect_elf

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", nargs="?", type=Path,
                        default=ROOT / "game/original/libspiderman.so")
    parser.add_argument("--manifest", type=Path,
                        default=ROOT / "docs/references/re02-original-fingerprints.json")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        expected = json.loads(args.manifest.read_text(encoding="utf-8"))
        actual = inspect_elf(args.reference, [s["name"] for s in expected["symbols"]])
        for key in ("size", "sha256", "machine", "symbols"):
            if actual[key] != expected[key]:
                raise ValueError(f"Reference mismatch in {key}; do not reuse the recorded addresses")
        actual["verified"] = True
        actual["ghidra_rebase_offset"] = expected["ghidra_rebase_offset"]
        actual["note"] = "Identity verification only; no ARM code was executed."
        text = json.dumps(actual, indent=2) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text, encoding="utf-8")
        print(text, end="")
        return 0
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f"Reference verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
