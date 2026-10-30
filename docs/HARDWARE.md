# Hardware and implementation references

Official documentation: https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85C
Resources: https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85C/Resources-And-Documents
Vendor examples: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C
V2 schematic: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.85C/ESP32-S3-Touch-LCD-1.85C_V2.pdf

| Peripheral | GPIO / configuration |
|---|---|
| ST77916 QSPI | CLK 40; D0 46; D1 45; D2 42; D3 41; CS 21; backlight 5 |
| Shared I2C | SDA 11; SCL 10; 400 kHz |
| CST816 touch | Address 0x15; polled; reset via expander P0 |
| TCA9554 | Address 0x20; LCD reset P1; other pins left as inputs |
| SDMMC 1-bit | CLK 14; CMD 17; D0 16; 20 MHz |
| I2S speaker | BCLK 48; WS 38; data out 47; stereo slots duplicate mono |
| V2 codec | ES8311 address 0x18; MCLK 2; amplifier enable 15 |
| USB | Native pins 19/20 reserved for USB |


Vendor ST77916 initialization, alternate panel table/ID detection, and ES8311 driver are included under src/vendor. The ES8311 Arduino I2C helpers were adapted to ESP-IDF's legacy I2C API. Their original Apache-2.0 license headers and Waveshare license are retained. LVGL and Espressif components retain their own licenses in components.

The QSPI transfer completion callback gates LVGL buffer reuse. Audio uses 12×512-frame DMA descriptors (128 ms at 48 kHz), separate SD file handles with read buffering, and a sample-count master clock. Conversion performs scaling/cropping so playback only decodes JPEGs. Late video frames may be dropped to preserve audio continuity; target frame rate is not guaranteed until tested on hardware.
