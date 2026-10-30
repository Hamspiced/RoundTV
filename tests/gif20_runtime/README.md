Host test of the actual 360x360 GIF renderer using LVGL 8.3.11 and a simulated clock.

```sh
cmake -S tests/gif20_runtime -B build-gif -DLVGL_DIR="$PWD/components/lvgl" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-gif --target gif20_check
build-gif/gif20_check sdcard/splash/splash.gif tests/gif20_runtime/crop.gif
```

Checks the supplied 6.9-second GIF over two loops, restart, clock rollover, late-frame accounting, centered crop and RGB565 colors. This is not a hardware frame-rate measurement.
