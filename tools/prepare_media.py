#!/usr/bin/env python3
"""Prepare RoundTV media without formatting or erasing any drive.

Requires Python 3.9+, FFmpeg/ffprobe; splash conversion also requires Pillow.
Writes via a temporary file and refuses replacement unless --overwrite is explicit.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


def run(args):
    subprocess.run([str(a) for a in args], check=True)


def probe(path):
    return json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(path)
    ]))


def validate_avi(path, prober=probe):
    """Enforce the deliberately small AVI profile understood by firmware."""
    size = path.stat().st_size
    if size >= 1_000_000_000:
        raise ValueError("AVI is too large. Increase --quality or reduce --fps and convert again.")
    with path.open("rb") as f:
        head = f.read(12)
    if len(head) != 12 or head[:4] != b"RIFF" or head[8:] != b"AVI " or struct.unpack_from("<I", head, 4)[0] + 8 != size:
        raise ValueError("Expected a single RIFF AVI, without OpenDML continuation.")
    streams = prober(path)["streams"]
    if len(streams) != 2:
        raise ValueError("AVI must contain exactly video and audio.")
    video, audio = streams
    if video.get("codec_name") != "mjpeg" or (video.get("width"), video.get("height")) != (360, 360):
        raise ValueError("Incorrect video codec or dimensions.")
    if audio.get("codec_name") != "pcm_s16le" or audio.get("channels") != 1 or audio.get("sample_rate") != "48000":
        raise ValueError("Incorrect audio profile.")


def safe_title(name):
    name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name).strip(" .")
    return (name or "Episode")[:100]


def output_guard(path, overwrite):
    if path.exists() and not overwrite:
        raise FileExistsError(f"Already exists: {path}. Use --overwrite to replace it.")
    path.parent.mkdir(parents=True, exist_ok=True)


def video(args, runner=run, prober=probe):
    source = args.input.resolve()
    if not source.is_file():
        raise FileNotFoundError(source)
    title = safe_title(args.title or source.stem)
    destination = args.sd / "videos" / "seasons" / f"{args.season:02d}" / f"{args.episode:02d} - {title}" / "video.avi"
    output_guard(destination, args.overwrite)
    if destination.resolve() == source:
        raise ValueError("Input and output must differ.")
    metadata = prober(source)
    if not any(s.get("codec_type") == "video" for s in metadata["streams"]):
        raise ValueError("Input has no video stream.")
    has_audio = any(s.get("codec_type") == "audio" for s in metadata["streams"])
    # Normalize anamorphic input to square pixels BEFORE fitting the video into the player.
    filters = (f"fps={args.fps},scale=trunc(iw*sar/2)*2:ih,setsar=1,"
               "scale=360:360:force_original_aspect_ratio=increase:force_divisible_by=2:flags=lanczos,"
               "crop=360:360:(iw-ow)/2:(ih-oh)/2,setsar=1")
    fd, temporary = tempfile.mkstemp(prefix=".roundtv-", suffix=".avi", dir=destination.parent)
    os.close(fd)
    try:
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "warning", "-y", "-i", source]
        if not has_audio:
            cmd += ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=mono"]
        cmd += ["-map", "0:v:0", "-map", "0:a:0" if has_audio else "1:a:0",
                "-vf", filters, "-c:v", "mjpeg", "-q:v", str(args.quality),
                "-pix_fmt", "yuvj420p", "-threads:v", "1",
                "-af", "aresample=48000:async=1:first_pts=0,alimiter=limit=0.95:level=false,apad", "-c:a", "pcm_s16le",
                "-ar", "48000", "-ac", "1", "-shortest", "-map_metadata", "-1",
                "-f", "avi", temporary]
        runner(cmd)
        validate_avi(Path(temporary), prober=prober)
        # Commit only a complete, validated conversion. Never modify the source file.
        os.replace(temporary, destination)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return destination


def splash(args):
    from PIL import Image
    source = args.input.resolve()
    if not source.is_file():
        raise FileNotFoundError(source)
    destination = args.sd / "splash" / "splash.gif"
    output_guard(destination, args.overwrite)
    if source == destination.resolve():
        raise ValueError("Use a separate source GIF; input and output must differ.")
    with tempfile.TemporaryDirectory(prefix="roundtv-gif-") as work:
        intermediate = Path(work) / "normalized.gif"
        # At most 20 fps. Palette and frame disposal are handled by FFmpeg and Pillow.
        filters = (f"fps={args.fps},scale=240:240:force_original_aspect_ratio=increase,"
                   "crop=240:240,split[a][b];[a]palettegen=max_colors=32[p];[b][p]paletteuse=dither=none")
        run(["ffmpeg", "-hide_banner", "-loglevel", "warning", "-y", "-i", source,
             "-filter_complex_threads", "1", "-filter_complex", filters, "-loop", "0", intermediate])
        with Image.open(intermediate) as im:
            if im.size != (240, 240):
                raise ValueError("Unexpected GIF dimensions.")
            for i in range(im.n_frames):
                im.seek(i)
                im.load()  # decode every frame before accepting it
            if im.n_frames > 2000:
                raise ValueError("GIF has too many frames; use a shorter animation.")
        if intermediate.stat().st_size > 2 * 1024 * 1024:
            raise ValueError("GIF exceeds 2 MiB. Use a shorter or simpler animation.")
        fd, tmp = tempfile.mkstemp(prefix=".roundtv-", suffix=".gif", dir=destination.parent)
        os.close(fd)
        try:
            shutil.copyfile(intermediate, tmp)
            os.replace(tmp, destination)
        finally:
            Path(tmp).unlink(missing_ok=True)
    print(destination)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    v = sub.add_parser("video", help="Convert one episode and create its SD folders")
    v.add_argument("input", type=Path)
    v.add_argument("--sd", type=Path, required=True, help="Existing SD mount or ordinary staging folder")
    v.add_argument("--season", type=int, required=True)
    v.add_argument("--episode", type=int, required=True)
    v.add_argument("--title")
    v.add_argument("--fps", type=int, choices=range(10, 31), default=24)
    v.add_argument("--quality", type=int, choices=range(2, 21), default=5, help="2=best/larger; 20=smaller")
    v.add_argument("--overwrite", action="store_true")
    v.set_defaults(func=video)
    s = sub.add_parser("splash", help="Normalize an animation for the full round display")
    s.add_argument("input", type=Path)
    s.add_argument("--sd", type=Path, required=True)
    s.add_argument("--fps", type=int, choices=range(1, 21), default=20)
    s.add_argument("--overwrite", action="store_true")
    s.set_defaults(func=splash)
    args = p.parse_args()
    if args.command == "video" and not (1 <= args.season <= 99 and 1 <= args.episode <= 999):
        p.error("Season must be 1-99 and episode 1-999.")
    for executable in ("ffmpeg", "ffprobe"):
        if not shutil.which(executable):
            p.error(f"{executable} is required and must be on PATH.")
    try:
        result = args.func(args)
        if result is not None:
            print(result)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        p.exit(1, f"Error: {exc}\n")


if __name__ == "__main__":
    import sys
    if len(sys.argv) == 1:
        from media_gui import launch
        launch()
    else:
        main()
