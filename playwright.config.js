import { defineConfig } from "@playwright/test";
export default defineConfig({
  testDir: "./web/tests/browser",
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  use: {
    baseURL: process.env.SITE_URL || "http://127.0.0.1:4173/RoundTV/",
    browserName: "chromium",
    launchOptions: process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {},
    screenshot: "only-on-failure"
  },
  projects: [
    { name: "desktop", use: { viewport: { width: 1280, height: 900 } } },
    { name: "mobile-layout", use: { viewport: { width: 390, height: 844 } } }
  ],
  webServer: process.env.SITE_URL ? undefined : {
    command: "npm run serve",
    url: "http://127.0.0.1:4173/RoundTV/",
    reuseExistingServer: !process.env.CI
  }
});
