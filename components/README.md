# Pinned runtime dependencies

- `lvgl/`: LVGL **8.3.11**, with the upstream MIT license in `LICENCE.txt`.
- `esp_new_jpeg/`: Espressif **0.6.0**, ESP32-S3 static library and headers, with its upstream license in `LICENSE`.
- Peripheral driver code in `src/vendor/` retains original headers and `LICENSE-Waveshare`.

The offline runtime copies were pruned: unused demos, examples, documentation, font-source/tooling assets, non-S3 JPEG libraries, and alternate build-platform support are omitted. LVGL CMake files are adjusted to build only the runtime for ESP-IDF and the host regression harnesses. The LVGL C/H runtime code and JPEG library/headers are otherwise unchanged. `examples/` and `demos/` are empty placeholders for upstream ESP-IDF include paths. The component manager is disabled in the root CMake file; these local sources are authoritative.

Web-runtime package versions, upstream licenses, and repository links are declared in their npm packages and the lockfile; generated JavaScript retains applicable linked legal notices. ESP Web Tools: https://github.com/esphome/esp-web-tools (Apache-2.0); Material Web: https://github.com/material-components/material-web (Apache-2.0); esptool-js: https://github.com/espressif/esptool-js (Apache-2.0).
