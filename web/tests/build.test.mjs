import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, cp, readFile, writeFile, rm, readdir } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { root, validateFirmware, buildSite, sha256 } from "../build.mjs";

test("only the verified S3 V2.1 merged release is accepted", async () => {
  const { release, bytes } = await validateFirmware();
  assert.equal(release.sha256, sha256(bytes));
  assert.equal(bytes.readUInt16LE(12), 9);
});

test("tampered bytes and unsupported hardware fail closed", async () => {
  const dir = await mkdtemp(path.join(os.tmpdir(), "roundtv-integrity-"));
  try {
    await cp(path.join(root, "firmware"), dir, { recursive: true });
    const image = path.join(dir, "v2/roundtv_box_merged.bin");
    const bytes = await readFile(image);
    bytes[bytes.length - 1] ^= 1;
    await writeFile(image, bytes);
    await assert.rejects(validateFirmware(dir), /checksum/);
    const info = JSON.parse(await readFile(path.join(dir, "release.json")));
    info.chipFamily = "ESP32-C5";
    await writeFile(path.join(dir, "release.json"), JSON.stringify(info));
    await assert.rejects(validateFirmware(dir), /Unsupported/);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});

test("static output has correct manifest, same-origin assets, and no source leakage", async () => {
  const dir = await mkdtemp(path.join(os.tmpdir(), "roundtv-site-"));
  try {
    await buildSite(dir);
    const manifest = JSON.parse(await readFile(path.join(dir, "manifest.json")));
    assert.equal(manifest.new_install_improv_wait_time, 0);
    assert.deepEqual(manifest.builds.map((b) => b.chipFamily), ["ESP32-S3"]);
    assert.equal(manifest.builds[0].parts[0].offset, 0);
    const release = JSON.parse(await readFile(path.join(dir, "release.json")));
    assert.equal(manifest.builds[0].parts[0].path, release.path);
    assert.equal(sha256(await readFile(path.join(dir, release.path))), release.sha256);
    assert.deepEqual((await readdir(dir)).sort(), [".nojekyll", "assets", "firmware", "index.html", "manifest.json", "release.json", "style.css"]);
    for (const file of await readdir(path.join(dir, "assets"))) {
      if (!file.endsWith(".js")) continue;
      const js = await readFile(path.join(dir, "assets", file), "utf8");
      assert.doesNotMatch(js, /import\(["']https?:/);
    }
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});
