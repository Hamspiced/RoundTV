import { build } from "esbuild";
import { createHash } from "node:crypto";
import { readFile, mkdir, writeFile, copyFile, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

export const root = fileURLToPath(new URL("../", import.meta.url));
export const sha256 = (data) => createHash("sha256").update(data).digest("hex");

export async function validateFirmware(directory = path.join(root, "firmware")) {
  const release = JSON.parse(await readFile(path.join(directory, "release.json"), "utf8"));
  if (release.version !== "0.2.6-box" || release.chipFamily !== "ESP32-S3" ||
      release.board !== "Waveshare ESP32-S3-Touch-LCD-1.85C-Box PCB V2.1" ||
      release.path !== "v2/roundtv_box_merged.bin" || !/^[a-f0-9]{64}$/.test(release.sha256)) {
    throw new Error("Unsupported release metadata");
  }
  const bytes = await readFile(path.join(directory, release.path));
  if (bytes.length !== release.bytes || sha256(bytes) !== release.sha256) {
    throw new Error("Firmware checksum/size mismatch");
  }
  const sums = await readFile(path.join(directory, "SHA256SUMS.txt"), "utf8");
  for (const [offset, filename] of [[0, "bootloader.bin"], [0x8000, "partition-table.bin"], [0x10000, "roundtv_box.bin"]]) {
    const data = await readFile(path.join(directory, "v2", filename));
    if (!sums.split("\n").includes(`${sha256(data)}  v2/${filename}`) ||
        !bytes.subarray(offset, offset + data.length).equals(data)) {
      throw new Error(`Invalid component/hash/offset: ${filename}`);
    }
  }
  if (!sums.split("\n").includes(`${release.sha256}  ${release.path}`) ||
      bytes[0] !== 0xe9 || bytes.readUInt16LE(12) !== 9 ||
      bytes[0x10000] !== 0xe9 || !bytes.includes(Buffer.from(release.version)) ||
      !bytes.includes(Buffer.from("1.85C V2.1 / ES8311"))) {
    throw new Error("Invalid ESP32-S3 merged image");
  }
  return { release, bytes };
}

export async function buildSite(destination = path.join(root, "dist")) {
  const { release, bytes } = await validateFirmware();
  await rm(destination, { recursive: true, force: true });
  await mkdir(path.join(destination, "firmware"), { recursive: true });
  const filename = `roundtv-r7-v2.1-${release.sha256.slice(0, 12)}.bin`;
  const manifest = {
    name: "RoundTV R7 — V2.1 Box",
    version: release.version,
    new_install_prompt_erase: true,
    new_install_improv_wait_time: 0,
    builds: [{ chipFamily: "ESP32-S3", parts: [{ path: `firmware/${filename}`, offset: 0 }] }]
  };
  await writeFile(path.join(destination, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
  await writeFile(path.join(destination, "release.json"), JSON.stringify({
    ...release, path: `firmware/${filename}`
  }, null, 2) + "\n");
  await writeFile(path.join(destination, "firmware", filename), bytes);
  await writeFile(path.join(destination, "firmware", `${filename}.sha256`), `${release.sha256}  ${filename}\n`);
  await copyFile(path.join(root, "web/index.html"), path.join(destination, "index.html"));
  await copyFile(path.join(root, "web/style.css"), path.join(destination, "style.css"));
  await build({
    entryPoints: [path.join(root, "web/main.js")], outdir: path.join(destination, "assets"),
    bundle: true, splitting: true, format: "esm", minify: true,
    target: ["chrome120", "edge120"], legalComments: "linked",
    chunkNames: "[name]-[hash]", assetNames: "[name]-[hash]", logLevel: "info"
  });
  await writeFile(path.join(destination, ".nojekyll"), "");
  console.log(`Verified ${release.version}: ${bytes.length} bytes, SHA-256 ${release.sha256}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await buildSite();
}
