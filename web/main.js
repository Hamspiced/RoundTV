const status = document.querySelector("#status");
const confirmation = document.querySelector("#confirm-board");
const installer = document.querySelector("#installer");
const flash = document.querySelector("#flash-button");
const placeholder = document.querySelector("#placeholder");
let ready = false;
let supported = false;

function message(text, kind = "") {
  status.textContent = text;
  status.dataset.kind = kind;
}

function updateGate() {
  const allowed = ready && supported && confirmation.checked;
  installer.hidden = !allowed;
  placeholder.hidden = allowed;
  flash.disabled = !allowed;
}

document.querySelector("#install-gate").addEventListener("click", (event) => {
  if (!(ready && supported && confirmation.checked)) {
    event.preventDefault();
    event.stopImmediatePropagation();
  }
}, true);
confirmation.addEventListener("change", updateGate);

async function prepare() {
  supported = window.isSecureContext && "serial" in navigator;
  if (!window.isSecureContext) {
    message("USB flashing requires HTTPS. Open the published secure flasher link.", "error");
    updateGate();
    return;
  } else if (!("serial" in navigator)) {
    message("Use desktop Google Chrome or Microsoft Edge for USB flashing. Downloads are still available below.", "warning");
  } else {
    message("Checking the firmware download and loading the USB installer…");
  }
  try {
    const response = await fetch("./release.json", { cache: "no-store" });
    if (!response.ok) throw new Error(`Release information unavailable (${response.status})`);
    const release = await response.json();
    if (release.chipFamily !== "ESP32-S3" || release.version !== "0.2.6-box" ||
        !/^firmware\/roundtv-r7-v2\.1-[a-f0-9]{12}\.bin$/.test(release.path) ||
        !/^[a-f0-9]{64}$/.test(release.sha256)) throw new Error("Unexpected release information");
    const firmware = await fetch(new URL(release.path, location.href), { cache: "no-store" });
    if (!firmware.ok) throw new Error(`Firmware download unavailable (${firmware.status})`);
    const bytes = await firmware.arrayBuffer();
    const digest = await crypto.subtle.digest("SHA-256", bytes);
    const hash = Array.from(new Uint8Array(digest), (v) => v.toString(16).padStart(2, "0")).join("");
    if (hash !== release.sha256 || bytes.byteLength !== release.bytes) throw new Error("Firmware integrity check failed; installation is disabled");
    document.querySelector("#checksum").textContent = hash;
    document.querySelector("#image-size").textContent = `${(release.bytes / 1024).toFixed(0)} KiB`;
    const download = document.querySelector("#download");
    download.href = release.path;
    download.hidden = false;
    document.querySelector("#checksum-download").href = `${release.path}.sha256`;
    document.querySelector("#checksum-download").hidden = false;
    if (supported) {
      await import("esp-web-tools/dist/install-button.js");
      await customElements.whenDefined("esp-web-install-button");
      ready = true;
      message("Firmware verified. Confirm your board below to enable USB installation.", "ready");
    }
  } catch (error) {
    ready = false;
    message(`${error.message}. Reload to retry.`, "error");
  }
  updateGate();
}

prepare();
