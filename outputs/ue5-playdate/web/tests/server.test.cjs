const test = require('node:test');
const assert = require('node:assert/strict');
const { createServer, resolveFile } = require('../server.cjs');
test('server confines public/model files and enables Wasm threads', async () => {
  assert.equal(resolveFile('/model/README.md'), null);
  assert.equal(resolveFile('/model/assets/%2e%2e%2f%2e%2e%2fREADME.md'), null);
  assert.equal(resolveFile('/%2e%2e%2fserver.cjs'), null);
  const server = createServer();
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const url = `http://127.0.0.1:${server.address().port}`;
    const response = await fetch(url);
    assert.equal(response.status, 200);
    assert.equal(response.headers.get('cross-origin-opener-policy'), 'same-origin');
    assert.equal(response.headers.get('cross-origin-embedder-policy'), 'require-corp');
    assert.match(await response.text(), /game-screen/);
    assert.equal((await fetch(url + '/model/README.md')).status, 403);
    assert.equal((await fetch(url, { method: 'POST' })).status, 405);
    assert.equal((await fetch(url + '/not-present.wasm')).status, 404);
  } finally { await new Promise(resolve => server.close(resolve)); }
});
