#!/usr/bin/env python3
"""Recover the verified original ARM reference from the supplied split project ZIP.

This tool reads only the archived game/original/libspiderman.so member. It
verifies the original split-volume sizes, ZIP member CRC, RE06 size and SHA-256
before writing anything. The recovered binary belongs under the ignored
game/original tree and must never be committed.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_MEMBER = "OpenAndroidUSM/game/original/libspiderman.so"
PAYLOAD_PATH = "game/original/libspiderman.so"


class SplitReader(io.RawIOBase):
    """Seekable read-only view that concatenates raw split-archive volumes."""

    def __init__(self, paths: list[Path]):
        super().__init__()
        self._files: list[io.BufferedReader] = []
        self._ends: list[int] = []
        self._pos = 0
        size = 0
        try:
            for path in paths:
                self._files.append(path.open("rb"))
                size += path.stat().st_size
                self._ends.append(size)
        except OSError:
            self.close()
            raise
        self._size = size

    def readable(self) -> bool:
        return True

    def seekable(self) -> bool:
        return True

    def tell(self) -> int:
        return self._pos

    def seek(self, offset: int, whence: int = 0) -> int:
        if whence not in (0, 1, 2):
            raise ValueError("Invalid seek origin")
        if whence == 0:
            new = offset
        elif whence == 1:
            new = self._pos + offset
        else:
            new = self._size + offset
        if new < 0:
            raise ValueError("Negative seek")
        self._pos = new
        return new

    def read(self, n: int = -1) -> bytes:
        if n < 0:
            n = self._size - self._pos
        n = max(0, min(n, self._size - self._pos))
        pieces: list[bytes] = []
        while n:
            index = bisect.bisect_right(self._ends, self._pos)
            if index >= len(self._files):
                raise OSError("Split archive read ran past the final volume")
            start = 0 if index == 0 else self._ends[index - 1]
            handle = self._files[index]
            handle.seek(self._pos - start)
            data = handle.read(min(n, self._ends[index] - self._pos))
            if not data:
                raise OSError("Split archive volume is truncated")
            pieces.append(data)
            self._pos += len(data)
            n -= len(data)
        return b"".join(pieces)

    def close(self) -> None:
        for handle in self._files:
            handle.close()
        self._files.clear()
        super().close()


def load_json(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"Manifest is not a JSON object: {path}")
    return value


def volume_paths(uploads_dir: Path, material: dict) -> list[Path]:
    specs = material.get("volumes")
    if not isinstance(specs, list) or not specs:
        raise ValueError("Original-material manifest has no split-volume list")
    paths: list[Path] = []
    names: set[str] = set()
    for spec in specs:
        if not isinstance(spec, dict):
            raise ValueError("Invalid split-volume manifest entry")
        name = spec.get("name")
        size = spec.get("size")
        if not isinstance(name, str) or not name or name in names:
            raise ValueError("Invalid or duplicate split-volume name")
        if not isinstance(size, int) or size < 0:
            raise ValueError(f"Invalid size for split volume {name}")
        names.add(name)
        path = uploads_dir / name
        if not path.is_file():
            raise ValueError(f"Missing split volume: {name}")
        actual_size = path.stat().st_size
        if actual_size != size:
            raise ValueError(
                f"Split volume size mismatch for {name}: expected {size}, got {actual_size}"
            )
        paths.append(path)
    return paths


def expected_reference(material: dict, fingerprints: dict) -> tuple[int, str, int]:
    size = fingerprints.get("size")
    sha256 = fingerprints.get("sha256")
    if not isinstance(size, int) or size <= 0:
        raise ValueError("Fingerprint manifest has an invalid reference size")
    if not isinstance(sha256, str) or len(sha256) != 64:
        raise ValueError("Fingerprint manifest has an invalid reference SHA-256")
    payload = material.get("payload")
    if not isinstance(payload, list):
        raise ValueError("Original-material manifest has no payload list")
    matches = [row for row in payload if isinstance(row, dict) and row.get("path") == PAYLOAD_PATH]
    if len(matches) != 1:
        raise ValueError("Original-material manifest does not identify exactly one reference library")
    row = matches[0]
    if row.get("size") != size or row.get("sha256") != sha256:
        raise ValueError("Reference identity disagrees between retained manifests")
    crc_text = row.get("zip_crc")
    if not isinstance(crc_text, str):
        raise ValueError("Original-material manifest has no reference ZIP CRC")
    try:
        crc = int(crc_text, 16)
    except ValueError as error:
        raise ValueError("Original-material manifest has an invalid reference ZIP CRC") from error
    return size, sha256.lower(), crc


def recover(
    uploads_dir: Path,
    output: Path,
    material_manifest: Path,
    fingerprint_manifest: Path,
    check_only: bool,
) -> tuple[int, str]:
    material = load_json(material_manifest)
    fingerprints = load_json(fingerprint_manifest)
    expected_size, expected_sha256, expected_crc = expected_reference(material, fingerprints)
    parts = volume_paths(uploads_dir, material)

    with SplitReader(parts) as split, zipfile.ZipFile(split) as archive:
        try:
            info = archive.getinfo(ARCHIVE_MEMBER)
        except KeyError as error:
            raise ValueError(f"Split archive is missing {ARCHIVE_MEMBER}") from error
        if info.is_dir():
            raise ValueError("Reference library archive entry is unexpectedly a directory")
        if info.file_size != expected_size:
            raise ValueError(
                f"Archived reference size mismatch: expected {expected_size}, got {info.file_size}"
            )
        if info.CRC != expected_crc:
            raise ValueError(
                f"Archived reference CRC mismatch: expected {expected_crc:08x}, got {info.CRC:08x}"
            )
        data = archive.read(info)

    actual_sha256 = hashlib.sha256(data).hexdigest()
    if len(data) != expected_size or actual_sha256 != expected_sha256:
        raise ValueError("Recovered reference does not match the retained RE06 identity")

    if check_only:
        return len(data), actual_sha256

    if output.exists():
        existing = output.read_bytes()
        if len(existing) == expected_size and hashlib.sha256(existing).hexdigest() == expected_sha256:
            return len(data), actual_sha256
        raise ValueError(
            f"Refusing to overwrite non-matching existing reference: {output}"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=output.name + ".", suffix=".tmp", dir=output.parent, delete=False
        ) as temporary:
            temporary.write(data)
            temporary.flush()
            temporary_path = Path(temporary.name)
        temporary_path.replace(output)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    return len(data), actual_sha256


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--uploads-dir",
        required=True,
        type=Path,
        help="directory containing OpenAndroidUSM.zip.001 ... .017",
    )
    parser.add_argument("--project", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--material-manifest",
        type=Path,
        default=ROOT / "RE06_verification" / "original-material-final.json",
    )
    parser.add_argument(
        "--fingerprint-manifest",
        type=Path,
        default=ROOT / "docs" / "references" / "re06-original-fingerprints.json",
    )
    parser.add_argument(
        "--check-only",
        action="store_true",
        help="verify the archived reference without writing game/original/libspiderman.so",
    )
    args = parser.parse_args()
    output = args.output or args.project.resolve() / PAYLOAD_PATH
    try:
        size, sha256 = recover(
            args.uploads_dir.resolve(),
            output.resolve(),
            args.material_manifest.resolve(),
            args.fingerprint_manifest.resolve(),
            args.check_only,
        )
        action = "verified" if args.check_only else "recovered"
        print(f"PASS: {action} {PAYLOAD_PATH}: {size} bytes SHA-256 {sha256}")
        if not args.check_only:
            print(f"Output: {output.resolve()}")
        return 0
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError, zipfile.BadZipFile) as error:
        print(f"Reference recovery failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
