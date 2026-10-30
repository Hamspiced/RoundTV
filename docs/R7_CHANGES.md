# R7 — bounded video dropping and remote diagnostics
Release `0.2.6-box` targets Waveshare ESP32-S3-Touch-LCD-1.85C-Box, including PCB V2.1 using the V2 image. The menu reads `ROUND TV R7`. Source, tests, and firmware now live at the repository root; the browser installer is linked in README.md.

## Motivation and limits
The reported failure is a frozen picture around ten minutes into playback while audio continues and controls still respond. These changes address identified firmware weaknesses; they have not been confirmed to eliminate the freeze on physical hardware.

The supplied episode decoded successfully on the host and the firmware AVI parser accepted all 63,694 video frames and 139,706,880 PCM samples. It contains 44:13.9 of video and 48:30.6 of PCM: the extra audio can retain the last picture near the end, but does not explain an early freeze. R7 does not rewrite or reconvert media.

## Playback changes
- A per-session `VideoPacing` helper retains the 100 ms late threshold but stops dropping once 250 ms of audio time has elapsed since the last published picture. First pictures and backward clock movement permit a picture. Skips yield for 1 ms.
- This bounds suppression by the dropping policy only. Slow scans, reads, decoding, scheduling, or publication can still delay pictures. No four-fps minimum or A/V recovery is guaranteed.
- FAT fast seek uses a 4096-word cluster-link map, approximately 16 KiB for each open read-only file. Map creation can increase startup time; highly fragmented maps can fall back to ordinary seeking. Map allocation failure can fail file-open.
- `Avi::read()` skips `fseek()` when `ftell()` already matches the requested offset, retaining bounds and read-error checks.
- No compressed-video queue or decoded-frame queue was added.

## Diagnostics
`player::Status` reports `video_frames`, `buffered_ms`, `read_max_ms`, and `video_stage`. They are copied from session atomics by the coordinator, including at completion. `buffered_ms` is queued PCM samples divided by 48, excluding the I2S DMA tail. `read_max_ms` is the longest completed video chunk scan or JPEG payload read, in milliseconds; it excludes file-open, decoding, and waiting.

The footer displays volume, whole seconds of buffered audio, published pictures, and stage, for example `50% B9s V14400 READ`. The counter resets per session and counts publication into the shared display buffer, not frames physically shown. A newer publication can replace an unread shared picture.

Stages: `0 WAIT` (audio readiness or presentation timing), `1 READ` (chunk scan or payload read), `2 DEC` (JPEG header/decode), `3 SHOW` (picture published), `4 END` (worker exited). Stages can change faster than the 50 ms UI refresh and are not a trace. `read_max_ms` is available in the public status but is not shown in the compact footer.

All nine controls remain visible without scrolling, including normal and wrapped-error states.

## Preserved behavior
The ten-second audio read-ahead limit, five-second prefill, stall recovery, codec unity gain, fixed V2 half-amplitude ceiling, 50% boot volume, pause/resume, and existing Box media profile remain unchanged. Legacy releases are archived outside the active repository.

## Remote acceptance
Flash `firmware/v2/roundtv_box_merged.bin` at `0x0` for PCB V2.1. Play a full episode on the actual board. If freezing recurs, capture the controls footer twice several seconds apart and report whether `V` advances. Increasing `V` points toward UI/display investigation; a stationary count and persistent stage narrow the investigation but are not conclusive. Host tests and compilation do not replace this hardware test.
