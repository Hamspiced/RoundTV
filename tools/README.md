# RoundTV 1.85C-Box Media Prep

A desktop GUI for preparing a whole folder of videos for your RoundTV firmware.
This is a Python/Tk application with a Mac double-click launcher, not a compiled
or signed standalone .app. Use only with the included 1.85C-Box firmware.

## Mac setup — once

With Homebrew installed, run:

```sh
brew install python@3.13 python-tk@3.13 ffmpeg
```

Then double-click **Launch RoundTV.command** in this folder. Keep the Python
files next to it. If Finder does not launch the script, open Terminal in this
folder and run:

```sh
bash "Launch RoundTV.command"
```

Alternatively, launch the GUI using `python3.13 prepare_media.py`.
The launcher checks Homebrew paths on Apple Silicon and Intel Macs.

Official dependency pages:
- https://formulae.brew.sh/formula/python@3.13
- https://formulae.brew.sh/formula/python-tk@3.13
- https://formulae.brew.sh/formula/ffmpeg

## Prepare a folder

1. Choose your **Video folder**. Subfolders are included; hidden folders and
   symlinks are skipped. Select one show's folder at a time.
2. Choose the **Output / SD root**: the mounted FAT32 SD card, or a separate
   staging folder whose contents you will copy to the card later. Source and
   output folders must be separate; neither may contain the other.
3. Leave automatic numbering enabled to recognize `Show.S01E02.mp4`,
   `Show_S01E02_Title.mkv`, `1x02`, `Season 01` / `S01` / `01` folders, and
   numbered filenames such as `02 - Episode.mp4`. Filename season/episode
   tags take precedence over folder names. Untagged files use the fallback
   season and next available episode numbers in natural order (2 before 10).
   Turn detection off to number the entire folder sequentially as one season.
4. Click **Scan folder**. Review the output paths; double-click a row to edit
   its season, episode or title. Remove selected rows to leave files out.
   Rescan after changing numbering settings. Duplicates block conversion until
   corrected. Specials tagged S00 need reassignment to season 1–99.
5. Click **Convert folder**. The upper bar tracks the current file; the lower
   bar tracks completed batch items. Save the report for any failures.

Output example:

```text
videos/seasons/01/02 - Episode Name/video.avi
```

Defaults match the firmware: MJPEG AVI, 360x360, 24 fps, mono signed 16-bit PCM
at 48000 Hz. Video is enlarged and center-cropped to fill the round display; the sides and circular corners are lost. Silent input
receives a silent audio track. Lower quality numbers produce clearer/larger
files; 5 is the default. Inputs can be MP4, MKV, MOV, AVI and other FFmpeg-readable
videos with supported filename extensions.

**Reconvert older 400x224 episodes:** choose a fresh output folder, or enable replacement. Existing outputs are skipped by default. Enable **Replace existing output files**
only when you want them replaced. Originals are never changed. Each conversion
is validated before the final file is replaced; failed/cancelled conversions
remove their temporary files. Cancellation preserves completed files. Restart
with the same settings to skip those completed files and continue the batch.

The app does not format or erase drives. Large libraries can take a long time
and use substantial SD space. Each output AVI must be under 1,000,000,000 bytes;
files above that are rejected. Firmware limits are 1,024 episodes in total and
512 per season; the planner checks the selected batch, not existing SD content.

## Existing command-line interface

The original commands remain available with arguments:

```sh
python3.13 prepare_media.py video input.mp4 --sd /Volumes/ROUNDTV --season 1 --episode 1
python3.13 prepare_media.py splash input.gif --sd /Volumes/ROUNDTV
python3.13 prepare_media.py --help
```

The GUI handles batch videos. The optional CLI splash command also needs Pillow
installed in the Python environment you use. It remains available separately.

## Validation

See [firmware validation](../docs/VALIDATION.md). From the repository root, run `python3 tools/tests/test_batch.py`; FFmpeg is required.
