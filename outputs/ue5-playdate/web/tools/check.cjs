'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
for (const folder of ['public', 'tools', 'tests']) {
  for (const name of fs.readdirSync(path.join(root, folder))) {
    if (!/\.(?:cjs|mjs|js)$/.test(name)) continue;
    const result = spawnSync(process.execPath, ['--check', path.join(root, folder, name)], { stdio: 'inherit' });
    if (result.status !== 0) process.exit(result.status || 1);
  }
}
const shared = spawnSync(process.execPath, ['--check', path.resolve(root, '../../playdate-local/interactions.js')], { stdio: 'inherit' });
if (shared.status !== 0) process.exit(shared.status || 1);
console.log('JavaScript syntax checks passed');
