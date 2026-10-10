let core, game, framePtr, audioPtr, errorPtr, menuPtr;
let busy = false, suspended = 0, lastFrame = 0, failed = false;
let saveTimer, saveQueue = Promise.resolve();
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const emit = (type, data = {}, transfer = []) => postMessage({ type, ...data }, transfer);
const database = new Promise((resolve, reject) => {
  const request = indexedDB.open('playdate-web-v1', 1);
  request.onupgradeneeded = () => request.result.createObjectStore('saves');
  request.onsuccess = () => resolve(request.result);
  request.onerror = () => reject(new Error('无法打开浏览器存档。请检查隐私模式或存储权限。'));
});
database.catch(() => {});
async function readSave(key) {
  const db = await database;
  return new Promise((resolve, reject) => {
    const request = db.transaction('saves').objectStore('saves').get(key);
    request.onsuccess = () => resolve(request.result || []);
    request.onerror = () => reject(request.error);
  });
}
async function writeSave(key, files) {
  const db = await database;
  return new Promise((resolve, reject) => {
    const transaction = db.transaction('saves', 'readwrite');
    transaction.objectStore('saves').put(files, key);
    transaction.oncomplete = resolve;
    transaction.onabort = transaction.onerror = () => reject(transaction.error || new Error('存档写入失败'));
  });
}
function snapshot(root, prefix = '') {
  const result = [];
  for (const name of core.FS.readdir(root)) {
    if (name === '.' || name === '..') continue;
    const path = `${root}/${name}`, relative = `${prefix}${name}`;
    if (core.FS.isDir(core.FS.stat(path).mode)) result.push(...snapshot(path, `${relative}/`));
    else result.push({ path: relative, data: core.FS.readFile(path).slice() });
  }
  return result;
}
function persist() {
  clearTimeout(saveTimer);
  if (!game) return saveQueue;
  const current = game;
  const files = snapshot(`/saves/${current.id}`);
  saveQueue = saveQueue.catch(() => {}).then(() => writeSave(current.bundle, files)).then(() => {
    emit('saved', { id: current.id, count: files.length });
  });
  return saveQueue;
}
function lastError() {
  core._crank_last_error(errorPtr, 2048);
  return core.UTF8ToString(errorPtr) || '游戏运行失败，请重新启动或选择另一款游戏。';
}
async function halt() {
  if (!game) return;
  core._crank_set_buttons(0);
  core._crank_web_request_stop();
  const deadline = performance.now() + 8000;
  while (!core._crank_web_can_join()) {
    if (performance.now() > deadline) throw new Error('游戏未能正常退出。请重新加载页面后重试。');
    await sleep(10);
  }
  core._crank_stop();
  await persist();
  game = null;
  suspended = 0;
  emit('stopped');
}
function validPath(name) {
  return typeof name === 'string' && name.length < 1000 && !name.startsWith('/') && !name.includes('\\')
    && !name.includes('\0') && name.split('/').every(part => part && part !== '.' && part !== '..');
}
async function fetchChecked(url) {
  const response = await fetch(url, { signal: AbortSignal.timeout(20000) });
  if (!response.ok) throw new Error(`游戏资源加载失败（${response.status}）。请检查本地游戏文件后重试。`);
  return response;
}
async function loadGame(id) {
  if (!['mahjong', 'music'].includes(id)) throw new Error('未知的游戏');
  const manifest = await (await fetchChecked(`./games/${id}/manifest.json`)).json();
  if (manifest.id !== id || !Array.isArray(manifest.files) || manifest.files.length > 20000) throw new Error('游戏清单无效');
  const total = manifest.files.reduce((sum, file) => sum + file.size, 0);
  if (total > 128 * 1024 * 1024 || manifest.files.some(file => !validPath(file.path) || !Number.isInteger(file.size) || file.size < 0)) throw new Error('游戏资源超出限制或路径无效');
  const files = [];
  let cursor = 0, received = 0;
  await Promise.all(Array.from({ length: 6 }, async () => {
    while (cursor < manifest.files.length) {
      const file = manifest.files[cursor++];
      const data = new Uint8Array(await (await fetchChecked(`./games/${id}/${file.path.split('/').map(encodeURIComponent).join('/')}`)).arrayBuffer());
      const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', data)), value => value.toString(16).padStart(2, '0')).join('');
      if (data.length !== file.size || hash !== file.sha256) throw new Error('游戏文件校验失败，请重新准备游戏资源。');
      files.push({ path: file.path, data });
      received += data.length;
      emit('progress', { id, received, total });
    }
  }));
  // Validate and fetch completely before terminating a healthy current game.
  await halt();
  core.FS.mkdirTree(`/games/${id}`);
  for (const file of files) {
    const target = `/games/${id}/${file.path}`;
    core.FS.mkdirTree(target.slice(0, target.lastIndexOf('/')));
    core.FS.writeFile(target, file.data);
  }
  const saveRoot = `/saves/${id}`;
  core.FS.mkdirTree(saveRoot);
  for (const file of await readSave(manifest.bundle)) {
    if (!validPath(file.path)) continue;
    const target = `${saveRoot}/${file.path}`;
    core.FS.mkdirTree(target.slice(0, target.lastIndexOf('/')));
    core.FS.writeFile(target, file.data);
  }
  game = manifest;
  failed = false;
  lastFrame = 0;
  if (!core.ccall('crank_start', 'number', ['string', 'string'], [`/games/${id}`, saveRoot])) {
    game = null;
    throw new Error(lastError());
  }
  const deadline = performance.now() + 15000;
  while (core._crank_status() === 1) {
    if (performance.now() > deadline) throw new Error('游戏启动超时，请重新加载页面后重试。');
    await sleep(20);
  }
  if (core._crank_status() === 3) throw new Error(lastError());
  if (core._crank_status() !== 2) throw new Error('游戏在启动时退出，请选择另一款游戏。');
  emit('started', { id, title: manifest.title, bundle: manifest.bundle });
  return { id, title: manifest.title };
}
function pump() {
  if (!core || !game || failed) return;
  const state = core._crank_status();
  if (state === 3 && !failed) { failed = true; emit('runtime-error', { message: lastError() }); }
  if (state !== 2) return;
  const serial = core._crank_copy_frame(framePtr, 400 * 240 * 4);
  if (serial && serial !== lastFrame) {
    lastFrame = serial;
    const pixels = core.HEAPU8.slice(framePtr, framePtr + 400 * 240 * 4);
    emit('frame', { serial, id: game.id, pixels }, [pixels.buffer]);
  }
  const length = core._crank_copy_audio(audioPtr, 8192);
  if (length) {
    const samples = core.HEAP16.slice(audioPtr / 2, audioPtr / 2 + length);
    emit('audio', { id: game.id, samples }, [samples.buffer]);
  }
}
const initialized = (async () => {
  const { default: createCore } = await import('./core/crank-core.mjs');
  core = await createCore({
    print: line => emit('log', { line }), printErr: line => emit('log', { line }),
    onAbort: reason => emit('runtime-error', { message: `模拟器启动失败：${reason}` })
  });
  framePtr = core._malloc(400 * 240 * 4);
  audioPtr = core._malloc(8192 * 2);
  errorPtr = core._malloc(2048);
  menuPtr = core._malloc(32768);
  core.FS.mkdirTree('/games');
  core.FS.mkdirTree('/saves');
  const changed = path => {
    if (game && path.startsWith('/saves/')) {
      clearTimeout(saveTimer);
      saveTimer = setTimeout(() => persist().catch(error => emit('save-error', { message: error.message })), 500);
    }
  };
  core.FS.trackingDelegate = { onWriteToFile: changed, onMovePath: (_, destination) => changed(destination) };
  setInterval(() => {
    try { pump(); }
    catch (error) { failed = true; core._crank_set_suspended(1); emit('runtime-error', { message: `画面或声音输出失败：${error.message}。请重新启动游戏。` }); }
  }, 10);
  emit('ready');
})();
initialized.catch(error => emit('runtime-error', { message: `无法启动模拟器：${error.message}。请确认已编译核心，并通过本地服务打开。` }));
self.addEventListener('message', async event => {
  const { requestId, action, payload = {} } = event.data;
  try {
    await initialized;
    let result;
    if (action === 'launch') {
      if (busy) throw new Error('正在切换游戏，请稍候。');
      busy = true;
      try { result = await loadGame(payload.id); } finally { busy = false; }
    } else if (action === 'buttons') core._crank_set_buttons(payload.mask >>> 0);
    else if (action === 'crank') core._crank_add_crank(Number(payload.delta) || 0, payload.docked ? 1 : 0);
    else if (action === 'suspend') { suspended = payload.reasons >>> 0; core._crank_set_suspended(suspended); }
    else if (action === 'save') result = await persist();
    else if (action === 'stop') { if (busy) throw new Error('正在切换游戏'); busy = true; try { await halt(); } finally { busy = false; } }
    else if (action === 'menu') {
      core._crank_set_menu_open(payload.open ? 1 : 0);
      if (payload.open) {
        await sleep(50);
        const bytes = core._crank_copy_menu(menuPtr, 32768);
        result = bytes > 0 && bytes <= 32768 ? JSON.parse(core.UTF8ToString(menuPtr)) : { items: [] };
      }
    } else if (action === 'menu-select') result = core._crank_menu_select(payload.id >>> 0, Number(payload.value) || 0);
    else throw new Error('未知操作');
    if (requestId) postMessage({ type: 'reply', requestId, result });
  } catch (error) {
    if (requestId) postMessage({ type: 'reply', requestId, error: error.message });
    else emit('runtime-error', { message: error.message });
  }
});
