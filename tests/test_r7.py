#!/usr/bin/env python3
"""Host regressions; synthetic video markers are not JPEG decoding tests."""
import array
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def chunk(tag, data):
    return tag + struct.pack("<I", len(data)) + data + b"\0" * (len(data) & 1)


def fixture(path):
    frames = 24 * 20 * 60
    video_header = bytearray(56)
    video_header[:8] = b"vidsMJPG"
    struct.pack_into("<III", video_header, 20, 1, 24, 0)
    struct.pack_into("<I", video_header, 32, frames)
    video_format = struct.pack("<IiiHH4sI", 40, 360, 360, 1, 24, b"MJPG", 0).ljust(40, b"\0")
    audio_header = bytearray(56)
    audio_header[:4] = b"auds"
    struct.pack_into("<III", audio_header, 20, 1, 48000, 0)
    struct.pack_into("<I", audio_header, 32, frames * 2000)
    struct.pack_into("<I", audio_header, 44, 2)
    audio_format = struct.pack("<HHIIHH", 1, 1, 48000, 96000, 2, 16)
    hdrl = chunk(b"LIST", b"hdrl" +
                 chunk(b"LIST", b"strl" + chunk(b"strh", video_header) + chunk(b"strf", video_format)) +
                 chunk(b"LIST", b"strl" + chunk(b"strh", audio_header) + chunk(b"strf", audio_format)))
    wave = array.array("H", range(65536))
    if sys.byteorder != "little":
        wave.byteswap()
    wave = wave.tobytes() * 2
    with path.open("wb") as out:
        out.write(b"RIFF\0\0\0\0AVI " + hdrl)
        movi = out.tell()
        out.write(b"LIST\0\0\0\0movi")
        for frame in range(frames):
            at = (frame * 2000 % 65536) * 2
            record = (chunk(b"00dc", struct.pack("<IIB", frame, frame ^ 0xffffffff, 0xa5)) +
                      chunk(b"JUNK", b"padding") + chunk(b"01wb", wave[at:at + 4000]))
            out.write(chunk(b"LIST", b"rec " + record) if frame % 97 == 0 else record)
        end = out.tell()
        out.seek(movi + 4)
        out.write(struct.pack("<I", end - movi - 8))
        out.seek(4)
        out.write(struct.pack("<I", end - 8))


def main():
    with tempfile.TemporaryDirectory(prefix="roundtv-r7-tests-") as temp:
        work = Path(temp)
        for name, sources, flags in [
            ("video_pacing_test", ["tests/video_pacing_test.cpp"], []),
            ("avi_long_test", ["tests/avi_long_test.cpp", "src/avi.cpp"], []),
            ("pcm_queue_test", ["tests/pcm_queue_test.cpp"], ["-pthread"]),
        ]:
            exe = work / name
            subprocess.run(["c++", "-std=c++17", "-O2", "-Wall", "-Wextra",
                            "-I", str(ROOT / "src"), *flags,
                            *(str(ROOT / source) for source in sources), "-o", str(exe)], check=True)
            args = [str(exe)]
            if name == "avi_long_test":
                media = work / "twenty-minutes.avi"
                fixture(media)
                args.append(str(media))
            subprocess.run(args, check=True)


if __name__ == "__main__":
    main()
