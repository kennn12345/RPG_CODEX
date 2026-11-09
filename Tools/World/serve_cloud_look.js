// Serves the cloud comparison pages (Tools/World/CloudLookPages) and the captures in Saved/CloudLook.
// Pages: compare.html (styles vs real photos), review.html?style=&before=&after=&refs=, review_all.html?before=&after=
const http = require("http"), fs = require("fs"), path = require("path");
const pages = path.resolve(__dirname, "CloudLookPages");
const shots = path.resolve(__dirname, "../../Saved/CloudLook");
const types = { ".html": "text/html; charset=utf-8", ".png": "image/png" };
http.createServer((req, res) => {
  const rel = decodeURIComponent(req.url.split("?")[0]).replace(/^\/+/, "") || "compare.html";
  const root = path.extname(rel) === ".html" ? pages : shots;
  const file = path.resolve(root, rel);
  if (!file.startsWith(root) || !fs.existsSync(file)) { res.writeHead(404); return res.end(); }
  res.writeHead(200, { "Content-Type": types[path.extname(file)] || "application/octet-stream" });
  fs.createReadStream(file).pipe(res);
}).listen(8765, "127.0.0.1");
