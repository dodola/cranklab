/* Run against npm start. Override PLAYWRIGHT_MODULE/CHROME_EXECUTABLE as needed.
   This is a real-game integration test, not an implementation-only screenshot. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'C:/Users/dodol/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const output = process.env.WEB_TEST_OUTPUT || 'D:/PlaydateAI/web-verification';
const origin = process.env.WEB_TEST_URL || 'http://127.0.0.1:4180/';
const report = { checks: [], errors: [] };
fs.mkdirSync(output, { recursive: true });
const hash = bytes => createHash('sha256').update(Buffer.from(bytes)).digest('hex');
async function snapshot(page) { return page.evaluate(() => window.__playdate.snapshot()); }
async function pixels(page) { return hash(await page.evaluate(() => window.__playdate.screen())); }
async function key(page, name) { await page.keyboard.press(name, { delay: 100 }); await page.waitForTimeout(150); }
async function launch(page, id) {
  if (await page.locator('#library').isHidden()) await page.locator('#library-toggle').click();
  await page.locator(`[data-game="${id}"]`).click();
  await page.waitForFunction(game => window.__playdate?.snapshot().game === game && !window.__playdate.snapshot().loading && window.__playdate.snapshot().frames > 3, id, { timeout: 45000 });
}
async function saves(page) {
  return page.evaluate(() => new Promise((resolve, reject) => {
    const request = indexedDB.open('playdate-web-v1', 1);
    request.onerror = () => reject(request.error);
    request.onsuccess = () => {
      const db = request.result, transaction = db.transaction('saves'), store = transaction.objectStore('saves');
      const result = store.getAll(); result.onerror = () => reject(result.error);
      result.onsuccess = () => resolve(result.result.map(files => files.map(file => ({ path: file.path, bytes: file.data.length }))));
      transaction.oncomplete = () => db.close();
    };
  }));
}
(async () => {
  const browser = await chromium.launch({ executablePath: process.env.CHROME_EXECUTABLE || 'C:/Program Files/Google/Chrome/Application/chrome.exe', headless: true, args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  try {
    const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
    const page = await context.newPage();
    page.on('pageerror', error => { if (!report.errors.includes(error.message)) report.errors.push(error.message); });
    await page.goto(origin);
    await page.waitForFunction(() => window.__playdate?.snapshot().ready && window.__playdate.snapshot().modelReady, null, { timeout: 40000 });
    assert.equal(await page.evaluate(() => crossOriginIsolated), true);
    await launch(page, 'mahjong');
    const start = await pixels(page);
    const modelPoints = await page.evaluate(() => window.__playdate.modelPoints());
    await page.mouse.move(modelPoints.A.x, modelPoints.A.y); await page.mouse.down();
    assert.equal((await snapshot(page)).mask, 32, 'clicking the actual 3D A mesh presses A');
    await page.waitForTimeout(110); await page.mouse.up(); await page.waitForTimeout(150);
    await page.keyboard.down('z');
    assert.ok((await page.evaluate(() => window.__playdate.modelButtons())).A.depth < -.008, 'Z depresses the real A mesh');
    await page.screenshot({ path: path.join(output, 'keyboard-a-held.png') });
    await page.mouse.move(modelPoints.A.x, modelPoints.A.y); await page.mouse.down();
    await page.keyboard.up('z');
    assert.equal((await snapshot(page)).mask, 32);
    await page.keyboard.down('z'); await page.mouse.up();
    assert.equal((await snapshot(page)).mask, 32);
    assert.ok((await page.evaluate(() => window.__playdate.modelButtons())).A.depth < -.008, 'pointer release preserves keyboard-owned mesh depression');
    await page.keyboard.up('z');
    await page.waitForFunction(() => Math.abs(window.__playdate.modelButtons().A.depth) < .0001);
    assert.equal((await snapshot(page)).mask, 0, 'visual feedback does not leave an echoed input owner');
    await page.keyboard.down('x');
    assert.ok((await page.evaluate(() => window.__playdate.modelButtons())).B.depth < -.008);
    await page.keyboard.up('x');
    await page.keyboard.down('w'); await page.keyboard.down('ArrowUp'); await page.keyboard.down('ArrowRight');
    let pad = (await page.evaluate(() => window.__playdate.modelButtons())).dpad;
    assert.ok(pad.depth < -.008 && pad.x < -.06 && pad.z > .06, 'combined directional keys tilt the real Dpad diagonally');
    await page.keyboard.up('w'); await page.keyboard.up('ArrowRight');
    pad = (await page.evaluate(() => window.__playdate.modelButtons())).dpad;
    assert.ok(pad.x < -.06 && Math.abs(pad.z) < .0001, 'ArrowUp preserves the shared W/Up hold');
    await page.keyboard.up('ArrowUp');
    await page.waitForFunction(() => Math.abs(window.__playdate.modelButtons().dpad.depth) < .0001);
    await page.keyboard.down('m');
    assert.ok((await page.evaluate(() => window.__playdate.modelButtons())).menu.depth < -.008);
    await page.keyboard.up('m');
    await page.locator('#intro-canvas').focus();
    await page.keyboard.down('z');
    assert.equal((await snapshot(page)).mask, 0, 'paused keys do not fall through to legacy canvas bindings');
    assert.ok(Math.abs((await page.evaluate(() => window.__playdate.modelButtons())).A.depth) < .0001);
    await page.keyboard.up('z'); await page.locator('#resume-game').click();
    await page.waitForFunction(() => Math.abs(window.__playdate.modelButtons().menu.depth) < .0001);
    report.checks.push('Keyboard A/B, menu and diagonal Dpad animate real meshes; mixed owners release without echoed input');
    assert.notEqual(await pixels(page), start, 'Mahjong A input must change the actual framebuffer');
    await page.screenshot({ path: path.join(output, 'mahjong-3d.png') });
    report.checks.push('Mahjong boots, produces frames/audio and responds to physical 3D A and keyboard input');

    const before = await snapshot(page);
    await page.mouse.dblclick(modelPoints.screen.x, modelPoints.screen.y, { delay: 90 });
    await page.waitForFunction(() => window.__playdate.snapshot().mode === 'immersive');
    const display = await page.locator('#game-screen').boundingBox();
    await page.mouse.dblclick(display.x + display.width / 2, display.y + display.height / 2, { delay: 90 });
    await page.waitForFunction(() => window.__playdate.snapshot().mode === 'model');
    assert.equal((await snapshot(page)).game, before.game);
    report.checks.push('Double tapping the real 3D LCD and immersive LCD switches view without restarting');
    await page.locator('#mode-toggle').click();
    await page.waitForFunction(() => window.__playdate.snapshot().mode === 'immersive');
    await page.waitForTimeout(200);
    const after = await snapshot(page);
    assert.equal(after.game, before.game); assert.ok(after.serial >= before.serial);
    await page.screenshot({ path: path.join(output, 'mahjong-immersive.png') });
    assert.ok((await page.locator('.dpad').boundingBox()).width >= 240);
    assert.ok((await page.locator('[data-button="A"]').boundingBox()).width >= 110);
    await page.keyboard.down('ArrowLeft'); await page.waitForTimeout(130);
    await page.screenshot({ path: path.join(output, 'immersive-dpad-held.png') });
    assert.equal(await page.locator('.dpad').evaluate(element => element.classList.contains('held')), true);
    await page.keyboard.up('ArrowLeft');
    assert.equal(await page.locator('.dpad').evaluate(element => element.classList.contains('held')), false);
    report.checks.push('3D/immersive switch preserves the live game session');

    await page.keyboard.down('z');
    const aButton = await page.locator('[data-button="A"]').boundingBox();
    await page.mouse.move(aButton.x + aButton.width / 2, aButton.y + aButton.height / 2); await page.mouse.down();
    await page.keyboard.up('z');
    assert.equal((await snapshot(page)).mask, 32);
    await page.mouse.up(); assert.equal((await snapshot(page)).mask, 0);
    report.checks.push('Independent touch/key holds release correctly');

    await launch(page, 'music');
    await key(page, 'ArrowDown'); await key(page, 'z');
    await page.screenshot({ path: path.join(output, 'music-open.png') });
    const quiet = (await snapshot(page)).nonzeroAudio;
    const image = await pixels(page);
    await page.keyboard.down('e'); await page.waitForTimeout(3200); await page.keyboard.up('e');
    await page.waitForTimeout(300);
    const music = await snapshot(page);
    assert.ok(music.crankAngle > 500, 'holding E must keep turning');
    assert.ok(music.nonzeroAudio > quiet + 10000, 'Music Box must produce audible PCM during crank playback');
    assert.notEqual(await pixels(page), image, 'Music Box crank must change actual pixels');
    await page.screenshot({ path: path.join(output, 'music-immersive.png') });
    report.checks.push('Music Box tune playback, real nonzero PCM and sustained crank input');

    const box = await page.locator('#game-screen').boundingBox();
    const angleBefore = music.crankAngle;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2); await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 + 70, { steps: 8 }); await page.mouse.up();
    assert.ok((await snapshot(page)).crankAngle > angleBefore + 50);
    const dial = await page.locator('#crank-dial').boundingBox();
    const previous = (await snapshot(page)).crankAngle;
    await page.mouse.move(dial.x + dial.width / 2, dial.y + 12); await page.mouse.down();
    await page.mouse.move(dial.x + dial.width - 12, dial.y + dial.height / 2, { steps: 10 }); await page.mouse.up();
    assert.ok((await snapshot(page)).crankAngle > previous + 40);
    report.checks.push('Immersive LCD swipe and rotary dial both drive the crank');

    await page.locator('#menu-toggle').click();
    await page.waitForTimeout(250);
    const paused = (await snapshot(page)).serial;
    await page.waitForTimeout(400);
    assert.equal((await snapshot(page)).serial, paused);
    assert.equal(await page.locator('#custom-menu button').count(), (await snapshot(page)).menuItems.length);
    await page.screenshot({ path: path.join(output, 'music-menu.png') });
    await page.locator('#resume-game').click(); await page.waitForTimeout(250);
    assert.ok((await snapshot(page)).serial > paused);
    report.checks.push('Menu renders the game-provided snapshot and pauses/resumes runtime');

    await page.locator('#library-toggle').click();
    await page.locator('[data-game="mahjong"]').click();
    await page.waitForFunction(() => window.__playdate.snapshot().game === 'mahjong' && !window.__playdate.snapshot().loading);
    const saved = await saves(page);
    assert.ok(saved.some(files => files.some(file => /config|tunes/.test(file.path) && file.bytes > 0)));
    await page.reload();
    await page.waitForFunction(() => window.__playdate?.snapshot().ready);
    assert.deepEqual(await saves(page), saved);
    await launch(page, 'music');
    await page.waitForTimeout(500);
    assert.ok((await snapshot(page)).logs.some(line => line.includes('Load config')));
    report.checks.push('Termination saves persist in IndexedDB, survive reload and restore on launch');

    for (const [name, width, height] of [['portrait', 390, 844], ['landscape', 844, 390]]) {
      await page.setViewportSize({ width, height }); await page.waitForTimeout(200);
      const bounds = await page.locator('#game-screen').boundingBox();
      assert.ok(bounds.width > width * (name === 'portrait' ? .8 : .45));
      for (const selector of ['#game-screen', '.dpad', '.ab-buttons', '#crank-dial', '#mode-toggle']) {
        const area = await page.locator(selector).boundingBox();
        assert.ok(area && area.x >= 0 && area.y >= 0 && area.x + area.width <= width + 1 && area.y + area.height <= height + 1, `${name}: ${selector} fits viewport`);
      }
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
      assert.ok((await page.locator('.dpad').boundingBox()).width >= (name === 'portrait' ? 185 : 150));
      assert.ok((await page.locator('[data-button="A"]').boundingBox()).width >= (name === 'portrait' ? 80 : 75));
      await page.screenshot({ path: path.join(output, `${name}.png`) });
    }
    report.checks.push('390×844 portrait and 844×390 landscape controls remain reachable');

    const touchContext = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
    const touchPage = await touchContext.newPage();
    await touchPage.addInitScript(() => localStorage.setItem('playdate-web-mode', 'immersive'));
    await touchPage.goto(origin); await touchPage.waitForFunction(() => window.__playdate?.snapshot().ready);
    await launch(touchPage, 'music');
    const touchBefore = (await snapshot(touchPage)).crankAngle;
    const cdp = await touchContext.newCDPSession(touchPage);
    const lcd = await touchPage.locator('#game-screen').boundingBox();
    const x = lcd.x + lcd.width / 2, y = lcd.y + 30;
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y, id: 1 }] });
    for (let i = 1; i <= 6; i++) await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x, y: y + i * 12, id: 1 }] });
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    assert.ok((await snapshot(touchPage)).crankAngle > touchBefore + 50);
    const touchA = await touchPage.locator('[data-button="A"]').boundingBox();
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: touchA.x + touchA.width / 2, y: touchA.y + touchA.height / 2, id: 2 }] });
    assert.equal((await snapshot(touchPage)).mask, 32);
    assert.equal(await touchPage.locator('[data-button="A"]').evaluate(element => element.classList.contains('held')), true);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchCancel', touchPoints: [] });
    assert.equal((await snapshot(touchPage)).mask, 0);
    const up = await touchPage.locator('[data-button="up"]').boundingBox();
    const left = await touchPage.locator('[data-button="left"]').boundingBox();
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: up.x + up.width / 2, y: up.y + up.height / 2, id: 3 }] });
    assert.equal((await snapshot(touchPage)).mask, 4);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: left.x + left.width / 2, y: left.y + left.height / 2, id: 3 }] });
    assert.equal((await snapshot(touchPage)).mask, 1);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    assert.equal((await snapshot(touchPage)).mask, 0);
    report.checks.push('Larger touch buttons depress, release on cancel and switch Dpad direction while sliding');
    report.checks.push('Real browser touch events swipe LCD to turn the crank');
    await touchContext.close();

    await page.route('**/games/mahjong/manifest.json', route => route.fulfill({ status: 503, body: 'Unavailable' }));
    await page.locator('#library-toggle').click(); await page.locator('[data-game="mahjong"]').click();
    await page.waitForFunction(() => !window.__playdate.snapshot().loading && !document.querySelector('#error-banner').hidden);
    assert.match(await page.locator('#error-text').textContent(), /503/);
    assert.equal((await snapshot(page)).game, 'music', 'failed fetch preserves the healthy current game');
    report.checks.push('Resource failure leaves current game intact with a visible recovery action');
    assert.deepEqual(report.errors, []);
    report.result = 'passed'; report.final = await snapshot(page);
    await context.close();
  } finally { await browser.close(); }
})().catch(error => { report.result = 'failed'; report.failure = error.stack; process.exitCode = 1; }).finally(() => {
  fs.writeFileSync(path.join(output, 'browser-results.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ result: report.result, checks: report.checks, errors: report.errors, failure: report.failure }, null, 2));
});
