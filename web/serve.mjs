import http from "node:http";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(fileURLToPath(new URL("../dist/", import.meta.url)));
const types = { ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css", ".json": "application/json", ".bin": "application/octet-stream" };
const server = http.createServer(async (req, res) => {
  try {
    let pathname = decodeURIComponent(new URL(req.url, "http://localhost").pathname);
    if (pathname.startsWith("/RoundTV/")) pathname = pathname.slice("/RoundTV".length);
    if (pathname.endsWith("/")) pathname += "index.html";
    const file = path.resolve(root, "." + pathname);
    if (!file.startsWith(root + path.sep)) throw new Error("Forbidden");
    const data = await readFile(file);
    res.writeHead(200, { "Content-Type": types[path.extname(file)] || "text/plain", "Cache-Control": "no-store" });
    res.end(data);
  } catch {
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("Not found");
  }
});
server.listen(Number(process.env.PORT || 4173), "127.0.0.1", () => console.log("RoundTV preview: http://127.0.0.1:4173/RoundTV/"));
