# RoundTV
A round-screen, SD-card TV player for the **Waveshare ESP32-S3-Touch-LCD-1.85C-Box, PCB V2.1**, using its ES8311 codec and built-in speaker. Current firmware: **R7 / 0.2.6-box**.

## Install over USB
Open **https://hamspiced.github.io/RoundTV/** in desktop Chrome or Edge. Connect with a USB data cable, confirm the PCB revision, and select **Connect & install R7**. No IDE or command-line tools are needed.

Only the V2.1 Box is supported. Other displays, older hardware revisions, and ESP32-C5 are not interchangeable. The flasher detects the ESP32-S3 chip, not the display/audio hardware; check your PCB before installing.

Flashing replaces internal firmware/settings, not the SD card. Existing Box R6/R7 media requires no reconversion. See [manual flashing and recovery](FLASH.md).

## Playback
- Native 360×360 MJPEG video, 24 fps conversion default, PCM16 mono 48 kHz audio.
- Five-second audio prefill, up to ten seconds queued in PSRAM, and stall recovery.
- ES8311 unity codec gain, fixed half-amplitude output cap, and 50% boot volume.
- Startup splash loops until tapped. Playback follows naturally numbered seasons/episodes; touch opens nine controls, including browsing, pause, volume, and next/previous.
- Splash stops during an episode and returns between episodes. Missing splash does not prevent starting playback.

R7 bounds late-frame dropping and enables FAT fast seeking. **It has not been confirmed to eliminate the reported physical video freeze.** The controls footer, for example `50% B9s V14400 READ`, shows volume, queued audio seconds, published-picture count, and worker stage. `V` does not count confirmed screen refreshes. If freezing recurs, send two photos several seconds apart and note whether `V` advances.

## Prepare media
Use a FAT32 SD card with `/splash/splash.gif` and `/videos/seasons/01/01 - Episode title/video.avi`. Firmware never formats the card.

The converter is in `tools/`: run `python3 tools/prepare_media.py` for its GUI, or use the CLI described in [tools/README.md](tools/README.md). Install FFmpeg/ffprobe and Pillow as described there. Source MP4/MKV files must be converted; arbitrary video formats are not supported on the board. Each AVI must be below 1 GB, with JPEG frames below 192 KiB.

## Develop
- [ESP-IDF 5.4 build and host tests](docs/BUILD.md)
- [Web flasher build/deployment](docs/WEB_FLASHER.md)
- [Hardware and vendor references](docs/HARDWARE.md)
- [R7 changes](docs/R7_CHANGES.md) and [validation limits](docs/VALIDATION.md)

The repository contains one hardware target, source/tests/converter, pinned runtime components with their licenses, and verified V2 firmware. Old releases, the 2.1-inch/Pico project, local environments, and generated logs are not part of this repository.
