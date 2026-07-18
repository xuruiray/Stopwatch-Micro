#!/usr/bin/env python3
"""Create a self-contained Stopwatch Micro flashing bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import zipfile
from pathlib import Path


VERSION_PATTERN = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?")
FLASH_FILES = (
    ("0x0", "bootloader.bin", Path("bootloader/bootloader.bin")),
    ("0x20000", "Stopwatch-Micro.bin", Path("Stopwatch-Micro.bin")),
    ("0x8000", "partition-table.bin", Path("partition_table/partition-table.bin")),
    ("0xd000", "ota_data_initial.bin", Path("ota_data_initial.bin")),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version", required=True, help="Semantic firmware version without a v prefix"
    )
    parser.add_argument("--commit", required=True, help="Git commit used for the build")
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if VERSION_PATTERN.fullmatch(args.version) is None:
        raise SystemExit(f"Invalid firmware version: {args.version}")
    if re.fullmatch(r"[0-9a-f]{40}", args.commit) is None:
        raise SystemExit("--commit must be a full 40-character Git SHA")

    missing = [
        str(args.build_dir / relative)
        for _, _, relative in FLASH_FILES
        if not (args.build_dir / relative).is_file()
    ]
    if missing:
        raise SystemExit(f"Missing build artifacts: {', '.join(missing)}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    bundle_name = f"Stopwatch-Micro-v{args.version}"
    bundle_dir = args.output_dir / bundle_name
    if bundle_dir.exists():
        shutil.rmtree(bundle_dir)
    bundle_dir.mkdir()

    for _, name, relative in FLASH_FILES:
        shutil.copy2(args.build_dir / relative, bundle_dir / name)

    flash_args = ["--flash_mode dio --flash_freq 80m --flash_size 16MB"]
    flash_args.extend(f"{offset} {name}" for offset, name, _ in FLASH_FILES)
    (bundle_dir / "flash_args").write_text("\n".join(flash_args) + "\n", encoding="utf-8")

    flashing = f"""# Flash Stopwatch Micro v{args.version}

This bundle targets the M5Stack StopWatch ESP32-S3 with 16 MB flash.

```bash
python -m esptool --chip esp32s3 -b 460800 \\
  --before default_reset --after hard_reset write_flash @flash_args
```

Verify the files before flashing with `shasum -a 256 -c SHA256SUMS`.
"""
    (bundle_dir / "FLASHING.md").write_text(flashing, encoding="utf-8")

    manifest = {
        "project": "Stopwatch Micro",
        "version": args.version,
        "commit": args.commit,
        "target": "esp32s3",
        "flash_size": "16MB",
        "files": [{"offset": offset, "name": name} for offset, name, _ in FLASH_FILES],
    }
    (bundle_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )

    checksum_files = sorted(
        path for path in bundle_dir.iterdir() if path.name != "SHA256SUMS"
    )
    checksums = "".join(f"{sha256(path)}  {path.name}\n" for path in checksum_files)
    (bundle_dir / "SHA256SUMS").write_text(checksums, encoding="utf-8")

    archive = args.output_dir / f"{bundle_name}.zip"
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(
        archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as package:
        for path in sorted(bundle_dir.iterdir()):
            package.write(path, Path(bundle_name) / path.name)

    archive_checksum = args.output_dir / f"{archive.name}.sha256"
    archive_checksum.write_text(f"{sha256(archive)}  {archive.name}\n", encoding="utf-8")
    print(f"Created {archive} ({archive.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
