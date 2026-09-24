#!/usr/bin/env python3
"""Synthetic tests for tools/recover_original_reference.py; no game data is used."""
from __future__ import annotations

import binascii
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "recover_original_reference.py"
MEMBER = "OpenAndroidUSM/game/original/libspiderman.so"
PAYLOAD = "game/original/libspiderman.so"


def run_tool(*arguments: str, expect_success: bool) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, str(TOOL), *arguments],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if (result.returncode == 0) != expect_success:
        raise RuntimeError(
            f"unexpected recovery exit {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def fixture(root: Path) -> tuple[Path, Path, Path, bytes]:
    uploads = root / "uploads"
    uploads.mkdir()
    reference = b"\x7fELF" + bytes(range(256)) * 19 + b"synthetic-reference"
    archive_path = root / "whole.zip"
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(MEMBER, reference)
        archive.writestr("OpenAndroidUSM/unrelated.txt", "not game data")
    archive_bytes = archive_path.read_bytes()
    cuts = [len(archive_bytes) // 3, 2 * len(archive_bytes) // 3, len(archive_bytes)]
    starts = [0] + cuts[:-1]
    volumes = []
    for index, (start, end) in enumerate(zip(starts, cuts), 1):
        name = f"OpenAndroidUSM.zip.{index:03d}"
        data = archive_bytes[start:end]
        (uploads / name).write_bytes(data)
        volumes.append({"name": name, "size": len(data)})
    sha256 = hashlib.sha256(reference).hexdigest()
    material = root / "material.json"
    material.write_text(json.dumps({
        "volumes": volumes,
        "payload": [{
            "path": PAYLOAD,
            "size": len(reference),
            "sha256": sha256,
            "zip_crc": f"{binascii.crc32(reference) & 0xffffffff:08x}",
        }],
    }), encoding="utf-8")
    fingerprints = root / "fingerprints.json"
    fingerprints.write_text(json.dumps({"size": len(reference), "sha256": sha256}), encoding="utf-8")
    return uploads, material, fingerprints, reference


def common_args(uploads: Path, material: Path, fingerprints: Path, project: Path) -> list[str]:
    return [
        "--uploads-dir", str(uploads),
        "--project", str(project),
        "--material-manifest", str(material),
        "--fingerprint-manifest", str(fingerprints),
    ]


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="usm-reference-recovery-") as temporary:
        root = Path(temporary)
        uploads, material, fingerprints, reference = fixture(root)
        project = root / "project"
        args = common_args(uploads, material, fingerprints, project)

        result = run_tool(*args, "--check-only", expect_success=True)
        if "PASS: verified" not in result.stdout or (project / PAYLOAD).exists():
            raise RuntimeError("check-only recovery wrote output or omitted success")

        result = run_tool(*args, expect_success=True)
        output = project / PAYLOAD
        if output.read_bytes() != reference or "PASS: recovered" not in result.stdout:
            raise RuntimeError("recovered reference differs from synthetic source")

        run_tool(*args, expect_success=True)

        output.write_bytes(b"do not overwrite")
        run_tool(*args, expect_success=False)
        if output.read_bytes() != b"do not overwrite":
            raise RuntimeError("non-matching existing output was overwritten")
        output.unlink()

        final_part = uploads / "OpenAndroidUSM.zip.003"
        saved = final_part.read_bytes()
        final_part.unlink()
        run_tool(*args, "--check-only", expect_success=False)
        final_part.write_bytes(saved + b"x")
        run_tool(*args, "--check-only", expect_success=False)
        final_part.write_bytes(saved)

        first_part = uploads / "OpenAndroidUSM.zip.001"
        original = bytearray(first_part.read_bytes())
        corrupt = bytearray(original)
        corrupt[max(0, len(corrupt) // 2)] ^= 0x01
        first_part.write_bytes(corrupt)
        run_tool(*args, "--check-only", expect_success=False)
        first_part.write_bytes(original)

        wrong = json.loads(fingerprints.read_text(encoding="utf-8"))
        wrong["sha256"] = "0" * 64
        fingerprints.write_text(json.dumps(wrong), encoding="utf-8")
        run_tool(*args, "--check-only", expect_success=False)

    print("Reference recovery synthetic tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
