import { test, expect } from "@playwright/test";

async function mockSerial(page) {
  await page.addInitScript(() => {
    window.portRequests = 0;
    Object.defineProperty(navigator, "serial", {
      configurable: true, value: {
        requestPort() {
          window.portRequests++;
          return Promise.reject(new DOMException("No port selected", "NotFoundError"));
        },
        getPorts: async () => []
      }
    });
  });
}

test("firmware verifies, board confirmation gates USB, and cancel is recoverable", async ({ page }) => {
  await mockSerial(page);
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("./");
  await expect(page.locator("#status")).toContainText("Firmware verified");
  await expect(page.locator("#installer")).toBeHidden();
  expect(await page.evaluate(() => window.portRequests)).toBe(0);
  await page.locator("#confirm-board").check();
  await expect(page.locator("#flash-button")).toBeEnabled();
  await page.locator("#flash-button").click();
  await expect.poll(() => page.evaluate(() => window.portRequests)).toBe(1);
  await expect(page.getByText("No port selected", { exact: true })).toBeVisible();
  await page.getByText("Cancel", { exact: true }).click();
  await expect(page.getByText("No port selected", { exact: true })).toBeHidden();
  expect(errors).toEqual([]);
});

test("a busy port is reported without starting installation", async ({ page }) => {
  await page.addInitScript(() => Object.defineProperty(navigator, "serial", {
    configurable: true, value: {
      requestPort: async () => ({ open: async () => { throw new Error("Port is already open"); } })
    }
  }));
  await page.goto("./");
  await expect(page.locator("#status")).toContainText("Firmware verified");
  await page.locator("#confirm-board").check();
  const alerted = page.waitForEvent("dialog").then(async (dialog) => {
    expect(dialog.message()).toContain("Port is already open");
    await dialog.accept();
  });
  await page.locator("#flash-button").click();
  await alerted;
  await expect(page.locator("ewt-install-dialog")).toHaveCount(0);
});

test("unchecking the board disables connection again", async ({ page }) => {
  await mockSerial(page);
  await page.goto("./");
  await expect(page.locator("#status")).toContainText("Firmware verified");
  await page.locator("#confirm-board").check();
  await expect(page.locator("#installer")).toBeVisible();
  await page.locator("#confirm-board").uncheck();
  await expect(page.locator("#installer")).toBeHidden();
  await expect(page.locator("#placeholder")).toBeDisabled();
});

test("unsupported browsers retain verified manual downloads but cannot flash", async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, "serial", { configurable: true, value: undefined });
    delete navigator.serial;
    delete Object.getPrototypeOf(navigator).serial;
  });
  await page.goto("./");
  await expect(page.locator("#status")).toContainText("desktop Google Chrome");
  await expect(page.locator("#download")).toBeVisible();
  await page.locator("#confirm-board").check();
  await expect(page.locator("#installer")).toBeHidden();
});

test("insecure contexts fail closed", async ({ page }) => {
  await page.addInitScript(() => Object.defineProperty(window, "isSecureContext", { value: false }));
  await page.goto("./");
  await expect(page.locator("#status")).toContainText("requires HTTPS");
  await page.locator("#confirm-board").check();
  await expect(page.locator("#installer")).toBeHidden();
});

test("corrupt firmware cannot enable installation", async ({ page }) => {
  await mockSerial(page);
  await page.route("**/firmware/*.bin", async (route) => {
    const response = await route.fetch();
    const bytes = await response.body();
    bytes[bytes.length - 1] ^= 1;
    await route.fulfill({ response, body: bytes });
  });
  await page.goto("./");
  await expect(page.locator("#status")).toContainText("integrity check failed");
  await page.locator("#confirm-board").check();
  await expect(page.locator("#installer")).toBeHidden();
});

test("failed downloads stay disabled and the layout has no horizontal overflow", async ({ page }) => {
  await mockSerial(page);
  await page.route("**/release.json", (route) => route.fulfill({ status: 503, body: "Unavailable" }));
  await page.goto("./");
  await expect(page.locator("#status")).toContainText("503");
  await expect(page.locator("#installer")).toBeHidden();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.locator("summary").click();
  await expect(page.getByText("To enter download mode:", { exact: false })).toBeVisible();
});
