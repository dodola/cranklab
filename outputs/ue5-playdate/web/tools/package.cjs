'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const output = path.join(root, 'dist');
const model = path.resolve(root, '../../playdate-local');
for (const required of ['public/core/crank-core.mjs', 'public/core/crank-core.wasm', 'public/games/catalog.json']) {
  if (!fs.existsSync(path.join(root, required))) throw new Error(`Missing ${required}: build the core and prepare games first`);
}
fs.mkdirSync(output, { recursive: true });
fs.cpSync(path.join(root, 'public'), path.join(output, 'public'), { recursive: true });
fs.mkdirSync(path.join(output, 'model'), { recursive: true });
for (const entry of ['assets', 'vendor', 'interactions.js']) fs.cpSync(path.join(model, entry), path.join(output, 'model', entry), {
  recursive: true, filter: file => !file.split(path.sep).includes('video')
});
for (const file of ['server.cjs', 'start.cmd', 'README.md']) fs.copyFileSync(path.join(root, file), path.join(output, file));
const hashes = [];
function visit(folder) {
  for (const file of fs.readdirSync(folder, { withFileTypes: true })) {
    const name = path.join(folder, file.name);
    if (file.isDirectory()) visit(name);
    else if (file.name !== 'SHA256SUMS.txt') hashes.push(`${crypto.createHash('sha256').update(fs.readFileSync(name)).digest('hex')}  ${path.relative(output, name).split(path.sep).join('/')}`);
  }
}
visit(output);
fs.writeFileSync(path.join(output, 'SHA256SUMS.txt'), hashes.join('\n') + '\n');
console.log(`Packaged ${hashes.length} files: ${output}`);
