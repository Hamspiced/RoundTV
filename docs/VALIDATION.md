# R7 validation — 2026-10-30

- Pacing: a simulated 30-minute on-time run has no dropped pictures. With persistent 500 ms lag beginning at ten minutes, the R6 rule publishes no further pictures while R7 permits 2,400 over the next ten minutes. The simulated steady-state gap is 250 ms; this is a policy test, not a hardware frame-rate guarantee.
- Timestamp cases: first-picture permission, exact 100 ms and 250 ms thresholds, backward clock movement, and timestamps above 2^50 microseconds pass.
- The actual AVI reader passes a synthetic 20-minute file with 28,800 video markers and 57,600,000 PCM samples, including the ten-minute boundary, nested records, odd-sized chunks, backward and adjacent reads, and bounds rejection. The markers are not JPEGs, so this does not validate device decoding or SD performance.
- The unchanged PCM queue passes its 20-minute simulation with a 12-second read stall and one recovery, preserving 57,600,123 samples; a threaded stress test preserves 20,000,000 more samples.
- The actual R7 controls and diagnostic formatter compile against LVGL 8.3.11. Normal and error renders pass button/label bounds checks, all nine controls remain non-scrolling, and representative footer values/stages fit.
- Existing startup/tap/catalog checks and all seven converter tests pass.
- The supported V2.1 target compiles with ESP-IDF 5.4.0 and GCC 14.2.0 (`esp-14.2.0_20241119`), retaining the existing SDK constraint. Compilation emits existing missing-initializer and LVGL enum-deprecation warnings; it is not warning-free.
- The cleaned V2.1 application is 722,384 bytes, with 89% of the 6 MiB application partition free. esptool validates application/bootloader checksums and SHA-256 validation digests. The merged image is checked against components at `0x0`, `0x8000`, and `0x10000`. Hashes are in `firmware/SHA256SUMS.txt`.
- Generated configuration contains fast seek enabled with a 4096-word table. Source/tests and firmware are now at the repository root; only PCB V2.1 is retained.
- No physical board was available or flashed. These changes have not been confirmed to eliminate the reported freeze. Full-episode playback, real SD/decoder timing, audio output, and interpretation of a freeze-time footer remain remote hardware acceptance work.
