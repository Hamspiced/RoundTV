#!/usr/bin/env python3
"""Verify and package a supported ESP-IDF build without connecting to hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    config = json.loads((build / "config/sdkconfig.json").read_text())
    if not config.get("FATFS_USE_FASTSEEK") or config.get("FATFS_FAST_SEEK_BUFFER_SIZE") != 4096:
        raise ValueError("Build must enable FAT fast seeking with a 4096-word table")
    commands = json.loads((build / "compile_commands.json").read_text())
    board = next((c for c in commands if Path(c["file"]).resolve() == ROOT / "src/board.cpp"), None)
    if not board or "ROUNDTV_BOX_V1=1" in board["command"] or config.get("IDF_TARGET") != "esp32s3":
        raise ValueError("Expected the supported V2.1 build from this source tree")
    version = re.search(r'set\(PROJECT_VER "([^"]+)"\)', (ROOT / "CMakeLists.txt").read_text()).group(1)
    for name in ("roundtv_box.bin", "bootloader/bootloader.bin"):
        report = subprocess.check_output([
            sys.executable, "-m", "esptool", "--chip", "esp32s3",
            "image_info", "--version", "2", str(build / name)
        ], text=True)
        if report.count("(valid)") != 2 or "(invalid)" in report:
            raise ValueError(f"Invalid image: {name}")
        if name == "roundtv_box.bin" and f"App version: {version}" not in report:
            raise ValueError("Build version does not match source")
    out = ROOT / "firmware/v2"
    out.mkdir(parents=True, exist_ok=True)
    for source in ("roundtv_box.bin", "bootloader/bootloader.bin", "partition_table/partition-table.bin"):
        shutil.copy2(build / source, out / Path(source).name)
    merged = out / "roundtv_box_merged.bin"
    subprocess.run([
        sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge_bin",
        "-o", str(merged), "--flash_mode", "dio", "--flash_size", "16MB", "--flash_freq", "80m",
        "0x0", str(out / "bootloader.bin"), "0x8000", str(out / "partition-table.bin"),
        "0x10000", str(out / "roundtv_box.bin")
    ], check=True)
    image = merged.read_bytes()
    for offset, name in ((0, "bootloader.bin"), (0x8000, "partition-table.bin"), (0x10000, "roundtv_box.bin")):
        data = (out / name).read_bytes()
        if image[offset:offset + len(data)] != data:
            raise ValueError(f"Incorrect merged offset: {name}")
    hashes = {str(p.relative_to(ROOT / "firmware")): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sorted(out.glob("*.bin"))}
    (ROOT / "firmware/SHA256SUMS.txt").write_text("".join(f"{h}  {p}\n" for p, h in hashes.items()))
    release = {"version": version, "chipFamily": "ESP32-S3",
               "board": "Waveshare ESP32-S3-Touch-LCD-1.85C-Box PCB V2.1",
               "path": "v2/roundtv_box_merged.bin", "bytes": len(image),
               "sha256": hashlib.sha256(image).hexdigest()}
    (ROOT / "firmware/release.json").write_text(json.dumps(release, indent=2) + "\n")
    print(f"Packaged {version}: {len(image)} bytes; {release['sha256']}")


if __name__ == "__main__":
    main()
