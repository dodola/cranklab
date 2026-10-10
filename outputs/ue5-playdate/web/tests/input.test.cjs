const test = require('node:test');
const assert = require('node:assert/strict');
test('releasing one touch preserves another owner of the same button', async () => {
  const { HeldButtons, BUTTONS } = await import('../public/input.mjs');
  const updates = [];
  const input = new HeldButtons(mask => updates.push(mask));
  input.set('key-z', 'A'); input.set('finger-2', 'A'); input.set('finger-3', 'left');
  input.set('key-z', null);
  assert.equal(input.mask, BUTTONS.A | BUTTONS.left);
  input.set('finger-2', null);
  assert.equal(input.mask, BUTTONS.left);
  input.releaseAll();
  assert.equal(input.mask, 0);
  assert.deepEqual(updates, [32, 33, 1, 0]);
});
test('dial crosses zero without a 360-degree jump', async () => {
  const { angleDelta } = await import('../public/input.mjs');
  assert.equal(angleDelta(179, -179), 2);
  assert.equal(angleDelta(-179, 179), -2);
  assert.equal(angleDelta(10, 40), 30);
});
