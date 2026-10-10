import { HeldButtons, KEY_BUTTONS, angleDelta } from './input.mjs';

const $ = selector => document.querySelector(selector);
const screen = $('#game-screen'), context = screen.getContext('2d', { alpha: false });
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
const tokens = getComputedStyle(document.documentElement);
const lcdColor = tokens.getPropertyValue('--lcd').trim(), lcdInk = tokens.getPropertyValue('--lcd-ink').trim();
const rgb = hex => { const value = parseInt(hex.slice(1), 16); return [value >> 16 & 255, value >> 8 & 255, value & 255]; };
const whitePixel = rgb(lcdColor), blackPixel = rgb(lcdInk);
const metrics = { frames: 0, audioSamples: 0, nonzeroAudio: 0, saves: 0, serial: 0, logs: [], mask: 0, crankAngle: 0, game: null, mode: 'immersive', ready: false };
let worker, requestId = 0, audioContext, audioNode, gainNode, audioReady;
let readyResolve, readyReject, boot;
const requests = new Map();
let currentGame, loading = false, menuOpen = false, muted = false, docked = true;
let angle = 0, velocity = 0, previousTime = performance.now();
const crankKeys = new Set();
let viewer, interactions, screenTexture, modelReady = false, modelPromise, screenMesh;
let lastPixels, mode = localStorage.getItem('playdate-web-mode') || 'model';
let lockOwned = false, releaseLock;

function error(message) { $('#error-text').textContent = message; $('#error-banner').hidden = false; }
function status(message) { $('#session-status').textContent = message; }
function rpc(action, payload = {}, wait = true) {
  const id = wait ? ++requestId : undefined;
  const result = wait ? new Promise((resolve, reject) => {
    const timer = setTimeout(() => { requests.delete(id); reject(new Error('操作超时，请重新加载页面后重试。')); }, 45000);
    requests.set(id, { resolve, reject, timer });
  }) : undefined;
  worker?.postMessage({ requestId: id, action, payload });
  return result;
}
function setupWorker() {
  boot = new Promise((resolve, reject) => { readyResolve = resolve; readyReject = reject; });
  boot.catch(() => {});
  if (!crossOriginIsolated || typeof SharedArrayBuffer === 'undefined') {
    const message = '浏览器线程支持未启用。请使用项目提供的本地服务，或为部署站点配置 COOP/COEP。';
    error(message); readyReject(new Error(message)); return;
  }
  worker = new Worker(new URL('./runtime-worker.mjs', import.meta.url), { type: 'module' });
  worker.addEventListener('error', event => { error(`模拟器无法启动：${event.message}。请重新加载页面。`); readyReject(new Error(event.message)); });
  worker.addEventListener('message', ({ data }) => {
    if (data.type === 'reply') {
      const request = requests.get(data.requestId);
      if (request) { clearTimeout(request.timer); requests.delete(data.requestId); if (data.error) request.reject(new Error(data.error)); else request.resolve(data.result); }
    } else if (data.type === 'ready') {
      metrics.ready = true; readyResolve(); status('准备就绪');
    } else if (data.type === 'frame') {
      if (data.id !== currentGame || loading) return;
      metrics.frames++; metrics.serial = data.serial;
      const pixels = new Uint8ClampedArray(data.pixels.buffer);
      for (let i = 0; i < pixels.length; i += 4) {
        const white = pixels[i] > 127;
        const color = white ? whitePixel : blackPixel;
        pixels[i] = color[0]; pixels[i + 1] = color[1]; pixels[i + 2] = color[2];
      }
      lastPixels = new ImageData(pixels, 400, 240);
      paintFrame();
    } else if (data.type === 'audio') {
      if (data.id !== currentGame || loading || menuOpen || document.hidden || !audioNode) return;
      metrics.audioSamples += data.samples.length;
      for (const sample of data.samples) if (sample !== 0) metrics.nonzeroAudio++;
      audioNode.port.postMessage({ samples: data.samples }, [data.samples.buffer]);
    } else if (data.type === 'started') {
      currentGame = data.id; metrics.game = data.id;
      metrics.frames = metrics.audioSamples = metrics.nonzeroAudio = metrics.saves = metrics.serial = 0;
      $('#game-title').textContent = data.title;
      document.title = `${data.title} · Playdate`;
    } else if (data.type === 'progress') {
      $('#load-progress').max = data.total || 1; $('#load-progress').value = data.received;
      $('#load-label').textContent = `正在准备游戏 · ${Math.round(data.received / Math.max(data.total, 1) * 100)}%`;
    } else if (data.type === 'saved') {
      metrics.saves = data.count;
      if (!loading && !menuOpen && data.id === currentGame) status('存档已保存在此浏览器');
    } else if (data.type === 'save-error') error(`存档未能保存：${data.message}。请检查浏览器存储空间。`);
    else if (data.type === 'runtime-error') { error(data.message); status('运行失败'); readyReject(new Error(data.message)); }
    else if (data.type === 'log') { metrics.logs.push(data.line.slice(0, 1000)); if (metrics.logs.length > 100) metrics.logs.shift(); console.debug('[Playdate]', data.line.slice(0, 1000)); }
  });
}
async function unlockAudio() {
  if (!audioReady) {
    audioContext = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
    audioReady = audioContext.audioWorklet.addModule('./audio-worklet.js').then(() => {
      audioNode = new AudioWorkletNode(audioContext, 'playdate-audio', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
      gainNode = audioContext.createGain();
      gainNode.gain.value = muted ? 0 : 1;
      audioNode.connect(gainNode).connect(audioContext.destination);
    });
    audioReady.catch(() => { audioReady = undefined; });
  }
  // Called directly from click/keydown so the browser can grant audio activation.
  const resumed = audioContext.resume();
  await Promise.all([audioReady, resumed]);
}
async function acquireLock() {
  if (lockOwned || !navigator.locks) return;
  await new Promise((resolve, reject) => {
    navigator.locks.request('playdate-web-player', { ifAvailable: true }, async lock => {
      if (!lock) { reject(new Error('另一个标签页正在运行游戏。请关闭那个页面后重试，以免覆盖存档。')); return; }
      lockOwned = true; resolve();
      await new Promise(done => { releaseLock = done; });
    }).catch(reject);
  });
}
function setLoading(value) {
  loading = value;
  $('#load-state').hidden = !value;
  $('#library').setAttribute('aria-busy', String(value));
  for (const button of document.querySelectorAll('[data-game]')) button.disabled = value;
  $('#menu-toggle').disabled = value || !currentGame;
}
async function launch(id) {
  if (loading) return;
  const sound = unlockAudio();
  setLoading(true); releaseInputs(); $('#error-banner').hidden = true;
  $('#load-progress').removeAttribute('value'); $('#load-label').textContent = '正在启动模拟器…';
  status('正在准备游戏…');
  try {
    await Promise.all([boot, sound, acquireLock()]);
    audioNode?.port.postMessage({ type: 'reset' });
    await rpc('launch', { id });
    lastPixels = undefined;
    setLoading(false);
    menuOpen = false; $('#system-menu').hidden = true; $('#menu-toggle').setAttribute('aria-expanded', 'false');
    setLibrary(false);
    $('#status-dot').classList.add('running');
    for (const card of document.querySelectorAll('[data-game]')) card.setAttribute('aria-current', String(card.dataset.game === currentGame));
    setDocked(true);
    status('游戏运行中 · 存档自动保存在此浏览器');
    (mode === 'model' ? $('#intro-canvas') : screen).focus();
  } catch (failure) {
    error(failure.message); status('未能启动，请重试'); setLoading(false); setLibrary(true);
  }
}
function canPlay() { return !!currentGame && !loading && !menuOpen && $('#library').hidden && !document.hidden; }
function syncButtonVisuals() {
  const buttons = new Set(held.sources.values());
  for (const button of document.querySelectorAll('[data-button]')) button.classList.toggle('held', buttons.has(button.dataset.button));
  const pad = $('.dpad');
  pad.style.setProperty('--tilt-x', `${(Number(buttons.has('up')) - Number(buttons.has('down'))) * 6}deg`);
  pad.style.setProperty('--tilt-y', `${(Number(buttons.has('right')) - Number(buttons.has('left'))) * 6}deg`);
  pad.classList.toggle('held', ['up', 'down', 'left', 'right'].some(name => buttons.has(name)));
  if (interactions) {
    if (buttons.size && mode === 'model') interactions.stopIdle();
    interactions.setButtonVisuals(buttons);
  }
}
const held = new HeldButtons(mask => {
  metrics.mask = mask;
  if (canPlay() || mask === 0) rpc('buttons', { mask }, false);
  syncButtonVisuals();
});
function releaseInputs() {
  held.releaseAll(); crankKeys.clear(); velocity = 0;
  if (interactions) { interactions.crankKeys.clear(); interactions.crankVelocity = 0; interactions.targetCrankAngle = interactions.crankAngle; interactions.setButtonVisual(interactions.parts.menu, false); for (const mesh of Array.from(interactions.pressed)) interactions.release(mesh); }
}
function suspend() {
  if (!worker || !metrics.ready) return;
  const reasons = (menuOpen || !$('#library').hidden ? 1 : 0) | (document.hidden || !document.hasFocus() ? 2 : 0);
  rpc('suspend', { reasons }, false);
  audioNode?.port.postMessage({ type: 'reset' });
}
function setLibrary(open) {
  if (open) menuRevision++;
  if (open && menuOpen) { menuOpen = false; rpc('menu', { open: false }, false); }
  $('#library').hidden = !open; $('#library-toggle').setAttribute('aria-expanded', String(open));
  releaseInputs(); suspend();
  if (open) { $('#system-menu').hidden = true; menuOpen = false; $('#menu-toggle').setAttribute('aria-expanded', 'false'); }
}
function paintFrame() {
  if (lastPixels) context.putImageData(lastPixels, 0, 0);
  if (screenTexture) screenTexture.needsUpdate = true;
}
function welcome() {
  context.fillStyle = lcdColor; context.fillRect(0, 0, 400, 240);
  context.fillStyle = lcdInk; context.textAlign = 'center'; context.font = 'bold 39px Segoe UI, sans-serif'; context.fillText('Playdate', 200, 104);
  context.font = '16px Microsoft YaHei, sans-serif'; context.fillText('选一款游戏，开始玩。', 200, 144);
  context.strokeStyle = lcdInk; context.strokeRect(12, 12, 376, 216);
}
function setDocked(value) {
  docked = value;
  $('#dock-toggle').textContent = value ? '展开摇柄' : '收起摇柄';
  $('#dock-toggle').setAttribute('aria-pressed', String(value));
  if (interactions && interactions.crankOpen === value) interactions.toggleCrank();
  if (canPlay()) rpc('crank', { delta: 0, docked }, false);
}
function turn(delta, fromModel = false) {
  if (!canPlay()) return;
  if (docked) setDocked(false);
  angle += delta; metrics.crankAngle = angle;
  $('#dial-hand').style.transform = `rotate(${angle}deg)`;
  const normalized = Math.round((angle % 360 + 360) % 360) % 360;
  $('#crank-dial').setAttribute('aria-valuenow', String(normalized));
  $('#crank-dial').setAttribute('aria-valuetext', `${normalized} 度`);
  if (interactions && !fromModel) {
    interactions.crankAngle = interactions.targetCrankAngle = angle;
    viewer.app.controllers.crank.setRotationAngle(angle);
  }
  rpc('crank', { delta, docked: false }, false);
}
function tick(time) {
  const dt = Math.min((time - previousTime) / 1000, .05); previousTime = time;
  const direction = Number(crankKeys.has('e')) - Number(crankKeys.has('q'));
  velocity += (direction * 300 - velocity) * (1 - Math.exp(-12 * dt));
  if (Math.abs(velocity) > .1 && canPlay()) turn(velocity * dt);
  requestAnimationFrame(tick);
}
function bindButton(button) {
  button.addEventListener('pointerdown', event => {
    if (!canPlay() || event.button !== 0) return;
    event.preventDefault(); button.setPointerCapture(event.pointerId);
    held.set(`pointer-${event.pointerId}`, button.dataset.button);
    void unlockAudio().catch(failure => error(`声音无法启动：${failure.message}`));
  });
  if (button.closest('.dpad')) button.addEventListener('pointermove', event => {
    if (!button.hasPointerCapture(event.pointerId)) return;
    const target = document.elementFromPoint(event.clientX, event.clientY)?.closest('.dpad [data-button]');
    held.set(`pointer-${event.pointerId}`, target?.dataset.button || null);
  });
  const release = event => held.set(`pointer-${event.pointerId}`, null);
  for (const name of ['pointerup', 'pointercancel', 'lostpointercapture']) button.addEventListener(name, release);
  button.addEventListener('keydown', event => {
    if (canPlay() && [' ', 'Enter'].includes(event.key)) { event.preventDefault(); held.set(`button-${button.dataset.button}`, button.dataset.button); }
  });
  button.addEventListener('keyup', event => { if ([' ', 'Enter'].includes(event.key)) held.set(`button-${button.dataset.button}`, null); });
  button.addEventListener('blur', () => held.set(`button-${button.dataset.button}`, null));
}
let dialPointer;
function dialAngle(event) { const box = $('#crank-dial').getBoundingClientRect(); return Math.atan2(event.clientX - box.left - box.width / 2, -(event.clientY - box.top - box.height / 2)) * 180 / Math.PI; }
$('#crank-dial').addEventListener('pointerdown', event => {
  if (!canPlay() || event.button !== 0 || dialPointer) return;
  event.preventDefault(); event.currentTarget.setPointerCapture(event.pointerId);
  dialPointer = { id: event.pointerId, angle: dialAngle(event) }; setDocked(false);
});
$('#crank-dial').addEventListener('pointermove', event => {
  if (dialPointer?.id !== event.pointerId) return;
  const next = dialAngle(event); turn(angleDelta(dialPointer.angle, next)); dialPointer.angle = next;
});
for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) $('#crank-dial').addEventListener(type, event => { if (dialPointer?.id === event.pointerId) dialPointer = undefined; });
$('#crank-dial').addEventListener('wheel', event => { event.preventDefault(); turn(event.deltaY * .3); }, { passive: false });
$('#crank-dial').addEventListener('keydown', event => {
  if (['ArrowLeft', 'ArrowDown', 'ArrowRight', 'ArrowUp'].includes(event.key)) { event.preventDefault(); event.stopPropagation(); turn(['ArrowLeft', 'ArrowDown'].includes(event.key) ? -15 : 15); }
});

let swipe, lastTap;
screen.addEventListener('pointerdown', event => {
  if (event.button !== 0 || swipe) return;
  screen.setPointerCapture(event.pointerId);
  swipe = { id: event.pointerId, x: event.clientX, y: event.clientY, lastY: event.clientY, time: performance.now(), moved: false };
});
screen.addEventListener('pointermove', event => {
  if (swipe?.id !== event.pointerId) return;
  if (Math.abs(event.clientX - swipe.x) + Math.abs(event.clientY - swipe.y) > 6) swipe.moved = true;
  if (swipe.moved) turn((event.clientY - swipe.lastY) * 1.2);
  swipe.lastY = event.clientY;
});
function finishSwipe(event) {
  if (swipe?.id !== event.pointerId) return;
  if (event.type === 'pointerup' && !swipe.moved && performance.now() - swipe.time < 260) {
    const tap = { x: event.clientX, y: event.clientY, time: performance.now() };
    if (lastTap && tap.time - lastTap.time < 320 && Math.hypot(tap.x - lastTap.x, tap.y - lastTap.y) < 30) { lastTap = undefined; void setMode('model'); }
    else lastTap = tap;
  } else lastTap = undefined;
  swipe = undefined;
}
for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) screen.addEventListener(type, finishSwipe);

async function initModel() {
  if (modelPromise) return modelPromise;
  modelPromise = (async () => {
    if (document.readyState !== 'complete') await new Promise(resolve => addEventListener('load', resolve, { once: true }));
    const canvas = $('#intro-canvas'), host = $('#model-host');
    const box = host.getBoundingClientRect();
    viewer = new ModelViewer({ canvas, width: Math.max(box.width, 320), height: Math.max(box.height, 420), zoom: 1.15, shadow: true });
    viewer.app.renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    await Promise.race([viewer.load({ basePath: '/model/', crossOrigin: 'anonymous' }), new Promise((_, reject) => setTimeout(() => reject(new Error('掌机模型加载超时')), 30000))]);
    viewer.setRotation(5, -12); viewer.start(); viewer.enableControls();
    interactions = new DeviceInteractions(viewer, canvas, reducedMotion);
    const toggle = interactions.toggleCrank.bind(interactions);
    interactions.toggleCrank = () => { toggle(); docked = !interactions.crankOpen; $('#dock-toggle').textContent = docked ? '展开摇柄' : '收起摇柄'; $('#dock-toggle').setAttribute('aria-pressed', String(docked)); if (canPlay()) rpc('crank', { delta: 0, docked }, false); };
    screenTexture = new THREE.Texture(screen);
    screenTexture.flipY = viewer.app.screenMaterial.uniforms.sTextureAlbedoMap.value?.flipY ?? true;
    screenTexture.minFilter = screenTexture.magFilter = THREE.NearestFilter;
    screenTexture.generateMipmaps = false;
    screenTexture.needsUpdate = true;
    viewer.app.screenMaterial.uniforms.sTextureAlbedoMap.value = screenTexture;
    viewer.app.screenMaterial.defines.USE_ALBEDOMAP = true;
    viewer.app.screenMaterial.needsUpdate = true;
    const main = interactions.main;
    const materialIndex = main.material.findIndex(material => material.name === 'asheville_screen');
    const geometry = main.geometry.clone();
    geometry.groups = geometry.groups.filter(group => group.materialIndex === materialIndex);
    screenMesh = new THREE.Mesh(geometry, main.material);
    new ResizeObserver(() => { const b = host.getBoundingClientRect(); if (b.width && b.height) { viewer.setSize(b.width, b.height); interactions.resize(); } }).observe(host);
    canvas.addEventListener('playdate-input', ({ detail }) => {
      if (!canPlay()) return;
      if (detail.type === 'crank') turn(detail.delta, true);
      else if (detail.button === 'menu') { if (detail.pressed) void toggleMenu(); }
      else if (detail.button === 'sleep') { if (detail.pressed) setLibrary(true); }
      else {
        held.set(`mesh-${detail.button}`, detail.pressed ? detail.button : null);
        // A physical release can leave the aggregate mask unchanged when a
        // keyboard owner is still holding the same key. Keep its visual down.
        syncButtonVisuals();
      }
    });
    const hitScreen = event => {
      interactions.pick(event);
      screenMesh.matrixWorld.copy(main.matrixWorld);
      const hit = interactions.intersectMeshes([screenMesh]);
      const occluder = interactions.intersectMeshes([main, ...Object.values(interactions.parts), ...interactions.crankMeshes]);
      return hit && (!occluder || hit.distance <= occluder.distance + .0001);
    };
    let modelTap;
    canvas.addEventListener('pointerdown', event => { modelTap = hitScreen(event) ? { x: event.clientX, y: event.clientY, time: performance.now() } : null; });
    canvas.addEventListener('pointerup', event => {
      if (!modelTap || Math.hypot(event.clientX - modelTap.x, event.clientY - modelTap.y) > 6 || performance.now() - modelTap.time > 260) { lastTap = undefined; return; }
      const tap = { x: event.clientX, y: event.clientY, time: performance.now() };
      if (lastTap && tap.time - lastTap.time < 320 && Math.hypot(tap.x - lastTap.x, tap.y - lastTap.y) < 30) { lastTap = undefined; void setMode('immersive'); }
      else lastTap = tap;
      modelTap = null;
    });
    canvas.addEventListener('pointercancel', () => { modelTap = lastTap = null; });
    canvas.addEventListener('webglcontextlost', event => { event.preventDefault(); error('3D 画面已中断，可以切换到沉浸模式继续游戏。'); });
    modelReady = true; syncButtonVisuals(); $('#model-loading').hidden = true;
    if (mode !== 'model') viewer.app.pause();
  })();
  return modelPromise;
}
async function setMode(next) {
  releaseInputs(); mode = next; metrics.mode = next;
  document.body.dataset.mode = next;
  $('#play-surface').hidden = next === 'model'; $('#model-host').hidden = next !== 'model';
  $('#mode-toggle').textContent = next === 'model' ? '沉浸模式' : '3D 掌机'; $('#mode-toggle').setAttribute('aria-pressed', String(next === 'immersive'));
  localStorage.setItem('playdate-web-mode', next);
  if (next === 'model') {
    try { await initModel(); viewer.app.resume(); const box = $('#model-host').getBoundingClientRect(); viewer.setSize(box.width, box.height); interactions.resize(); paintFrame(); }
    catch (failure) { error(`3D 掌机无法加载：${failure.message}。沉浸模式仍可游玩。`); await setMode('immersive'); }
  } else if (modelReady) viewer.app.pause();
}
let menuRevision = 0;
async function toggleMenu() {
  if (!currentGame || loading) return;
  const revision = ++menuRevision;
  menuOpen = !menuOpen; releaseInputs();
  $('#system-menu').hidden = !menuOpen; $('#menu-toggle').setAttribute('aria-expanded', String(menuOpen));
  if (menuOpen) {
    $('#library').hidden = true; $('#library-toggle').setAttribute('aria-expanded', 'false');
    suspend();
    const custom = $('#custom-menu'); custom.replaceChildren();
    try {
      const menu = await rpc('menu', { open: true });
      if (revision !== menuRevision || !menuOpen) return;
      metrics.menuItems = (menu.items || []).map(item => ({ id: item.id, title: item.title, kind: item.kind }));
      for (const item of menu.items || []) {
        const button = document.createElement('button'); button.type = 'button'; button.className = 'menu-option';
        const title = document.createElement('span'); title.textContent = item.title;
        const value = document.createElement('span');
        value.textContent = item.kind === 1 ? (item.value ? '✓' : '○') : item.kind === 2 ? item.options?.[item.value - 1] || '' : '→';
        button.append(title, value);
        button.addEventListener('click', async () => {
          button.disabled = true;
          const nextValue = item.kind === 1 ? Number(!item.value) : item.kind === 2 ? item.value % item.options.length + 1 : 0;
          try { await rpc('menu-select', { id: item.id, value: nextValue }); await toggleMenu(); }
          catch (failure) { error(failure.message); button.disabled = false; }
        });
        custom.append(button);
      }
      $('#resume-game').focus(); status('游戏已暂停');
    } catch (failure) { error(failure.message); }
  } else {
    await rpc('menu', { open: false }); suspend(); status('游戏运行中');
    (mode === 'model' ? $('#intro-canvas') : screen).focus();
  }
  audioNode?.port.postMessage({ type: 'reset' });
}
document.addEventListener('keydown', event => {
  if (event.isComposing || event.target.matches('input, textarea, select') || event.target === $('#crank-dial')) return;
  const key = event.key.length === 1 ? event.key.toLowerCase() : event.key;
  if (key === 'Escape') { if (menuOpen) void toggleMenu(); else if (!$('#library').hidden && currentGame) setLibrary(false); return; }
  if (event.target === $('#intro-canvas') && (KEY_BUTTONS[key] || ['q', 'e', 'c', 'm'].includes(key))) {
    // Do not fall through to the viewer's independent bindings while paused.
    event.preventDefault(); event.stopImmediatePropagation();
  }
  if (!canPlay()) return;
  if (KEY_BUTTONS[key]) {
    event.preventDefault(); event.stopImmediatePropagation(); held.set(`key-${key}`, KEY_BUTTONS[key]);
  } else if (['q', 'e'].includes(key)) {
    event.preventDefault(); event.stopImmediatePropagation(); crankKeys.add(key); setDocked(false);
  } else if (key === 'c' && !event.repeat) { event.preventDefault(); event.stopImmediatePropagation(); setDocked(!docked); }
  else if (key === 'm' && !event.repeat) { event.preventDefault(); event.stopImmediatePropagation(); void toggleMenu(); if (interactions) interactions.setButtonVisual(interactions.parts.menu, true); }
}, true);
document.addEventListener('keyup', event => {
  const key = event.key.length === 1 ? event.key.toLowerCase() : event.key;
  // The host owns both keyboard edges. The original canvas keyup handler
  // would otherwise release a mesh still held by an independent pointer.
  if (KEY_BUTTONS[key] || ['q', 'e', 'c', 'm'].includes(key)) { event.preventDefault(); event.stopImmediatePropagation(); }
  held.set(`key-${key}`, null); crankKeys.delete(key);
  if (key === 'm' && interactions) interactions.setButtonVisual(interactions.parts.menu, false);
}, true);
for (const button of document.querySelectorAll('[data-button]')) bindButton(button);
async function toggleAudio() {
  muted = !muted; $('#audio-toggle').textContent = muted ? '开启声音' : '静音'; $('#audio-toggle').setAttribute('aria-pressed', String(muted));
  try { await unlockAudio(); gainNode.gain.setValueAtTime(muted ? 0 : 1, audioContext.currentTime); } catch (failure) { error(`声音无法启动：${failure.message}`); }
}
async function toggleFullscreen() {
  try { if (document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen(); }
  catch { error('此浏览器暂不支持全屏。可以继续使用沉浸模式。'); }
}
document.addEventListener('fullscreenchange', () => { $('#fullscreen-toggle').textContent = document.fullscreenElement ? '退出全屏' : '全屏'; $('#fullscreen-toggle').setAttribute('aria-pressed', String(!!document.fullscreenElement)); });
const actions = {
  library: () => setLibrary($('#library').hidden),
  'library-close': () => { setLibrary(false); $('#library-toggle').focus(); },
  mode: () => setMode(mode === 'model' ? 'immersive' : 'model'),
  menu: toggleMenu,
  restart: () => launch(currentGame),
  choose: async () => { if (menuOpen) await toggleMenu(); setLibrary(true); },
  dock: () => setDocked(!docked),
  audio: toggleAudio,
  fullscreen: toggleFullscreen,
  retry: () => { if (metrics.ready && currentGame) return launch(currentGame); location.reload(); },
  mahjong: () => launch('mahjong'), music: () => launch('music')
};
// Literal HTML actions route to this one dispatcher. This also gives assistive
// technology a click alternative to the pointer-held game controller buttons.
window.playdateUI = Object.freeze({
  activate: action => { const run = actions[action]; if (run) Promise.resolve(run()).catch(failure => error(failure.message)); },
  press: (button, event) => { if (!canPlay() || event.detail) return; held.set(`assist-${button}`, button); setTimeout(() => held.set(`assist-${button}`, null), 80); }
});
addEventListener('blur', () => { releaseInputs(); dialPointer = swipe = lastTap = undefined; suspend(); });
addEventListener('focus', suspend);
document.addEventListener('visibilitychange', () => { if (document.hidden) releaseInputs(); suspend(); });
addEventListener('pagehide', () => { releaseInputs(); rpc('save', {}, false); releaseLock?.(); });
window.__playdate = Object.freeze({
  snapshot: () => ({ ...metrics, logs: [...metrics.logs], docked, loading, menuOpen, modelReady, audioState: audioContext?.state }),
  screen: () => Array.from(context.getImageData(0, 0, 400, 240).data),
  modelButtons: () => modelReady ? Object.fromEntries(Object.entries(interactions.parts).map(([name, mesh]) => [name, {
    depth: mesh.position.y - mesh.userData.rest.y, x: mesh.rotation.x, z: mesh.rotation.z
  }])) : null,
  modelPoints: () => {
    if (!modelReady) return null;
    viewer.app.scene.updateMatrixWorld(true);
    const box = $('#intro-canvas').getBoundingClientRect();
    const project = point => { point.project(viewer.app.camera); return { x: box.left + (point.x + 1) * box.width / 2, y: box.top + (1 - point.y) * box.height / 2 }; };
    const points = Object.fromEntries(Object.entries(interactions.parts).map(([key, mesh]) => [key, project(mesh.getWorldPosition(new THREE.Vector3()))]));
    const positions = screenMesh.geometry.attributes.position, indices = screenMesh.geometry.index, center = new THREE.Vector3();
    let count = 0;
    for (const group of screenMesh.geometry.groups) for (let i = group.start; i < group.start + group.count; i++) { center.add(new THREE.Vector3().fromBufferAttribute(positions, indices.getX(i))); count++; }
    points.screen = project(center.divideScalar(count || 1).applyMatrix4(interactions.main.matrixWorld));
    return points;
  }
});
welcome(); setupWorker(); requestAnimationFrame(tick); void setMode(mode);
