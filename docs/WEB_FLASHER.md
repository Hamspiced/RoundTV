# USB web flasher

Published site: **https://hamspiced.github.io/RoundTV/**. Desktop Chrome/Edge, HTTPS, and a data-capable USB cable are required. Only Waveshare 1.85C-Box PCB V2.1 / ESP32-S3 is offered. The page requires explicit hardware confirmation; chip detection cannot identify the codec/display revision.

## Build and test
Use Node.js 22 or later:

```sh
npm ci --ignore-scripts
npm test
npm run build
npx playwright install chromium
npm run test:browser
npm run serve
```

The local preview is `http://127.0.0.1:4173/RoundTV/`; localhost is a secure-context exception for Web Serial. Browser tests mock serial access; they do not flash a board. `CHROME_PATH` can select an already installed Chrome executable for local tests.

`web/build.mjs` verifies release version, chip, hashes, component offsets, and merged firmware bytes. It builds only static content into `dist/`, bundles ESP Web Tools and its dynamic loader assets locally, and generates an ESP32-S3-only manifest with one image at offset 0. The content-hashed binary URL avoids accidentally reusing an old image. The page verifies its SHA-256 before enabling installation. Downloads, unsupported/insecure browsers, corrupted images, cancellation, and desktop/mobile layouts have regression coverage.

ESP Web Tools is pinned to 10.4.0, with compatible Material Web 2.2.0. Transitive versions and integrity hashes are in `package-lock.json`. No runtime CDN is required. Improv/Wi-Fi post-install probing is disabled because RoundTV does not implement it. Firmware/settings may be overwritten; SD media is unaffected.

## Deployment
`.github/workflows/pages.yml` runs integrity/build/browser checks and deploys only `dist/` through GitHub Pages on `main`. Pull requests run checks without deployment. Configure the repository's Pages source to GitHub Actions. No secrets, local archive, converter sources, or SDK files enter the website artifact.

## C5 flasher reference
The [WarHog C5 flasher change](https://github.com/olosnoks/warhog/commit/723dbc1e5d477ff2f53f112deb45a1a6d39753bb) was reviewed for USB connection, cancellation, reset, and ROM-loader behavior. Its no-stub path, SPI initialization, packet changes, and 8 MB flash parameters are C5-specific and are **not** applied to this S3/16 MB board. ESP Web Tools requests the port directly from the click gesture and its pinned reset path asserts RTS before the chip reset. S3 uses the normal loader; BOOT/RESET recovery instructions are shown. No C5 implementation code was copied.

Successful site tests do not establish physical USB-flashing reliability or resolution of the video freeze. Hardware acceptance still requires a complete install and full-episode playback.
