#!/usr/bin/env python3
"""Verify RE06 reference functions and CHostage's actual Audible virtual slots.

No ARM code is executed. The supplied image is never modified by this tool.
"""
from __future__ import annotations
import argparse
import json
import struct
import sys
from pathlib import Path
from verify_re01_reference import inspect_elf

ROOT = Path(__file__).resolve().parents[1]


def virtual_word(data: bytes, address: int) -> int:
    # inspect_elf has already validated this ELF32 header and section table.
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    shoff, shentsize, shnum = header[6], header[11], header[12]
    for index in range(shnum):
        sec = struct.unpack_from('<IIIIIIIIII', data, shoff + index * shentsize)
        _, kind, _, start, offset, size, *_ = sec
        if kind != 8 and start <= address and address + 4 <= start + size:
            at = offset + address - start
            if at + 4 > len(data):
                raise ValueError('Virtual word lies outside file-backed bytes')
            return struct.unpack_from('<I', data, at)[0]
    raise ValueError(f'No file-backed section covers virtual address {address:#x}')


def verify(reference: Path, manifest: Path) -> dict:
    expected = json.loads(manifest.read_text(encoding='utf-8'))
    actual = inspect_elf(reference, [s['name'] for s in expected['symbols']])
    for key in ('size', 'sha256', 'machine', 'symbols'):
        if actual[key] != expected[key]:
            raise ValueError(f'Reference mismatch in {key}; do not reuse RE06 evidence')
    raw = reference.read_bytes()
    by_name = {s['name']: s for s in actual['symbols']}
    table = expected['hostage_audible_vtable']
    slots = []
    for offset, name in table['slots'].items():
        address = int(table['vptr_elf'], 16) + int(offset, 16)
        target = by_name[name]
        expected_pointer = int(target['elf_address'], 16) | int(target['thumb'])
        pointer = virtual_word(raw, address)
        if pointer != expected_pointer:
            raise ValueError(f'Audible virtual slot {offset} does not resolve to {name}')
        slots.append({'offset': offset, 'slot_elf': f'0x{address:08x}',
                      'pointer': f'0x{pointer:08x}', 'name': name})
    actual.update(verified=True, vtable_slots=slots,
                  ghidra_rebase_offset=expected['ghidra_rebase_offset'],
                  note='Identity and virtual-slot verification only; no ARM execution.')
    return actual


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', nargs='?', type=Path,
                        default=ROOT/'game/original/libspiderman.so')
    parser.add_argument('--manifest', type=Path,
                        default=ROOT/'docs/references/re06-original-fingerprints.json')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        actual = verify(args.reference, args.manifest)
        text = json.dumps(actual, indent=2) + '\n'
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text, encoding='utf-8')
        print(text, end='')
        return 0
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f'Reference verification failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
