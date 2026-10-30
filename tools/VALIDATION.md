# Validation

Seven batch tests passed with real FFmpeg conversions for this 360x360 / 48 kHz profile. Coverage includes natural ordering, numbering conflicts, silent input, source preservation, skip/replace behavior, cancellation, and failed-conversion cleanup.

Run `python3 tests/test_batch.py` from this folder, with FFmpeg installed. The desktop GUI could not be opened in this environment; the macOS launcher and visual layout require a local check.
