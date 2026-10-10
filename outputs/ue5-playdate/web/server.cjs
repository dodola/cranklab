'use strict';
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, 'public');
const packagedModel = path.join(__dirname, 'model');
const model = fs.existsSync(packagedModel) ? packagedModel : path.resolve(__dirname, '../../playdate-local');
const types = { '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.json': 'application/json', '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.bin': 'application/octet-stream' };
function resolveFile(url) {
  const name = decodeURIComponent(new URL(url, 'http://localhost').pathname);
  const isModel = name.startsWith('/model/');
  const base = isModel ? model : root;
  const relativeName = isModel ? name.slice(7) : name === '/' ? 'index.html' : name.slice(1);
  // Only the maintained viewer, interactions and scene assets are public.
  if (isModel && !/^(vendor\/|assets\/|interactions\.js$)/.test(relativeName)) return null;
  const file = path.resolve(base, relativeName);
  const relative = path.relative(base, file);
  return relative.startsWith('..') || path.isAbsolute(relative) ? null : file;
}
function createServer() {
  return http.createServer((req, res) => {
    const security = { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp', 'Cross-Origin-Resource-Policy': 'same-origin', 'X-Content-Type-Options': 'nosniff' };
    if (!['GET', 'HEAD'].includes(req.method)) { res.writeHead(405, security).end(); return; }
    let file;
    try { file = resolveFile(req.url); }
    catch { res.writeHead(400, security).end(); return; }
    if (!file) { res.writeHead(403, security).end(); return; }
    fs.stat(file, (error, stat) => {
      if (error || !stat.isFile()) { res.writeHead(404, security).end('Not found'); return; }
      res.writeHead(200, { ...security, 'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Content-Length': stat.size, 'Cache-Control': 'no-cache' });
      if (req.method === 'HEAD') { res.end(); return; }
      const stream = fs.createReadStream(file);
      stream.on('error', () => res.destroy());
      res.on('close', () => stream.destroy());
      stream.pipe(res);
    });
  });
}
if (require.main === module) {
  const port = Number(process.env.PORT || 4180);
  const server = createServer();
  server.listen(port, '127.0.0.1', () => console.log(`Playdate Web: http://127.0.0.1:${port}`));
  server.on('error', error => { console.error(error.message); process.exitCode = 1; });
}
module.exports = { createServer, resolveFile };
