# Build firmware and run regressions

Use **ESP-IDF 5.4.0**, its ESP32-S3 Xtensa GCC 14.2.0 (`esp-14.2.0_20241119`), and an activated SDK Python environment. Only the PCB V2.1 hardware is supported. Pinned local components in `components/` are used without the component manager or a registry download.

From the repository root:

```sh
idf.py -B build build
python tools/package_firmware.py build
```

With CMake 4, add `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` to the idf.py invocation. Compilation and packaging never connect to a device. Packaging verifies application/bootloader checksums and SHA-256 digests, merges at 0x0/0x8000/0x10000, checks offsets, and updates `firmware/SHA256SUMS.txt` and `firmware/release.json`. Then run `npm test` and `npm run build` to validate the updated web-flasher image before publishing.

The SDK configuration retains 16 MB flash, octal PSRAM, 240 MHz CPU, native USB Serial/JTAG console, `CONFIG_FATFS_USE_FASTSEEK=y`, and `CONFIG_FATFS_FAST_SEEK_BUFFER_SIZE=4096`. Check the generated `build/config/sdkconfig.h` and JSON. Cluster maps consume about 16 KiB per read-only file, can increase open time, and fall back to ordinary seeking when too fragmented for the table. Allocation failure can fail file-open.

## Host checks
Requires Python 3, a C++17 compiler, FFmpeg/ffprobe, and CMake:

```sh
python3 tests/test_r7.py
python3 tests/test_startup.py
python3 tools/tests/test_batch.py
python3 tests/controls/generate.py
cmake -S tests/controls -B build-controls -DLVGL_DIR="$PWD/components/lvgl" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-controls --target controls_check
```

Run `build-controls/controls_check` normally and with `error` as an argument, using separate working directories for the output `controls.ppm`. The harness extracts actual controls and footer code and checks all nine buttons and representative diagnostic text against circular-screen bounds.

`tests/test_r7.py` checks 30-minute on-time pacing, sustained lag after ten minutes, clock boundaries, a synthetic 20-minute AVI, and the PCM queue stress test. Synthetic video contains markers, not JPEGs; it does not measure SD or device-decoder throughput.

The splash harness uses `tests/gif20_runtime` with the same LVGL CMake argument. Build `gif20_check` and run it with `sdcard/splash/splash.gif` and `tests/gif20_runtime/crop.gif`. Test artifacts/build directories are ignored.
