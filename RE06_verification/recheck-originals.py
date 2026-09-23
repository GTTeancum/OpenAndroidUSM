#!/usr/bin/env python3
"""Read-only original-material comparison against the supplied 17 split ZIP volumes.

Only members below game/data and game/original are read and CRC checked; this is
not a full CRC validation or extraction of the very large project archive.
"""
from __future__ import annotations
import argparse
import bisect
import hashlib
import io
import json
from pathlib import Path
import sys
import zipfile


class SplitReader(io.RawIOBase):
    def __init__(self, paths: list[Path]):
        super().__init__()
        self.files = []
        self.ends = []
        self.pos = 0
        size = 0
        try:
            for path in paths:
                self.files.append(path.open('rb'))
                size += path.stat().st_size
                self.ends.append(size)
        except OSError:
            self.close()
            raise
        self.size = size

    def readable(self): return True
    def seekable(self): return True
    def tell(self): return self.pos
    def seek(self, offset, whence=0):
        if whence not in (0, 1, 2): raise ValueError('Invalid seek origin')
        new = offset if whence == 0 else self.pos + offset if whence == 1 else self.size + offset
        if new < 0: raise ValueError('Negative seek')
        self.pos = new
        return new

    def read(self, n=-1):
        n = max(0, self.size-self.pos) if n < 0 else max(0, min(n, self.size-self.pos))
        pieces = []
        while n:
            index = bisect.bisect_right(self.ends, self.pos)
            start = 0 if index == 0 else self.ends[index-1]
            handle = self.files[index]
            handle.seek(self.pos-start)
            data = handle.read(min(n, self.ends[index]-self.pos))
            if not data: raise OSError('Split volume is truncated')
            pieces.append(data)
            self.pos += len(data)
            n -= len(data)
        return b''.join(pieces)

    def close(self):
        for handle in self.files: handle.close()
        super().close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uploads-dir', required=True, type=Path)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = args.project.resolve()
    try:
        parts = [args.uploads_dir/f'OpenAndroidUSM.zip.{i:03d}' for i in range(1,18)]
        rows = []
        seen = set()
        with SplitReader(parts) as split, zipfile.ZipFile(split) as archive:
            for info in archive.infolist():
                rel = info.filename.removeprefix('OpenAndroidUSM/')
                if info.is_dir() or not rel.startswith(('game/data/', 'game/original/')):
                    continue
                target = (root/rel).resolve()
                if root not in target.parents or rel in seen:
                    raise ValueError('Invalid or duplicate original member '+rel)
                seen.add(rel)
                data = archive.read(info)  # ZipFile verifies member CRC here.
                if target.read_bytes() != data:
                    raise ValueError('Original material differs: '+rel)
                rows.append({'path': rel, 'size': len(data),
                             'sha256': hashlib.sha256(data).hexdigest(),
                             'zip_crc': f'{info.CRC:08x}'})
        total = sum(row['size'] for row in rows)
        if len(rows) != 599 or total != 170642144:
            raise ValueError('Original-material selection does not match this checkpoint')
        result = {'verified': True, 'files': len(rows), 'bytes': total,
                  'compared_byte_for_byte': True, 'selected_zip_member_crc_checked': True,
                  'full_project_archive_crc_checked': False,
                  'volumes': [{'name': p.name, 'size': p.stat().st_size} for p in parts],
                  'scope': 'Original game/data and game/original only; no ARM execution.',
                  'payload': rows}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print(f'PASS: {len(rows)} original files, {total} bytes unchanged')
        return 0
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f'Original-material verification failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
