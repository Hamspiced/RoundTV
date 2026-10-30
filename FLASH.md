# Install RoundTV R7 — PCB V2.1

## Browser installer
1. Open **https://hamspiced.github.io/RoundTV/** in desktop Google Chrome or Microsoft Edge over HTTPS.
2. Check that the board is the **Waveshare ESP32-S3-Touch-LCD-1.85C-Box, PCB V2.1** with ES8311 audio. Do not use this image for other boards.
3. Connect its native USB-C port with a USB data cable. Close serial monitors and other flasher tabs.
4. Confirm the hardware checkbox, click **Connect & install R7**, choose the serial port, and complete the installer. Do not unplug during writing.
5. Reset or power-cycle if needed. The controls should show **ROUND TV R7** and boot at 50% volume.

The page verifies the firmware SHA-256 before enabling installation. A single merged ESP32-S3 image is written at **0x0**, including bootloader at 0x0, partitions at 0x8000, and application at 0x10000. Flashing can replace internal settings; the SD card is not formatted or modified. Existing Box media remains compatible.

## Manual installation
With esptool 4.x installed in a Python environment, replace `PORT` with the actual serial port and run from this repository:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x0 firmware/v2/roundtv_box_merged.bin
```

Do not write the standalone `roundtv_box.bin` at 0x0. Published component and merged hashes are in `firmware/SHA256SUMS.txt`.

## Recovery
If no port appears, use a known-good data cable and connect directly to the computer. Close serial apps, disconnect/reconnect USB, and retry. If download mode is needed, hold BOOT, press/release RESET, release BOOT, and retry. The port name may change. Press RESET with BOOT released afterward if it remains in download mode.

Canceling the chooser is safe. No development test connects to or flashes hardware. Full-episode playback remains the remote acceptance test. If the picture freezes while audio continues, photograph the controls footer twice several seconds apart and record whether the published-picture `V` counter increases.
