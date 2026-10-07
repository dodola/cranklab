'use strict';

const canvas = document.querySelector('#intro-canvas');
const container = document.querySelector('#device');
const video = document.querySelector('#video1');
const preference = window.matchMedia('(prefers-reduced-motion: reduce)');
let viewer;
let loaded = false;
let failed = false;
let screenReady = false;
let interactions;

function fail(message) {
  failed = true;
  viewer?.app.pause();
  video.pause();
  document.querySelector('#loading').hidden = true;
  document.querySelector('#error').hidden = false;
  document.querySelector('#error-message').textContent = message;
  document.querySelector('#retry').disabled = false;
}
function stopMotion() {
  viewer.app.isIdleAnimating = false;
  viewer.app.controls.setOrientationFromObject(viewer.app.device);
}
function playScreen() { return video.play().catch(() => { /* Pointer interaction retries blocked autoplay. */ }); }
function attachScreen() {
  if (!loaded || screenReady || failed) return;
  screenReady = true;
  viewer.showVideo();
  if (!preference.matches) void playScreen();
}

async function initialize() {
  try {
    if (typeof ModelViewer === 'undefined') throw new Error('查看器脚本未加载');
    const bounds = container.getBoundingClientRect();
    viewer = new ModelViewer({ canvas, width: bounds.width, height: bounds.height, zoom: 1.15, shadow: true,
      video: { element: video, src: './assets/video/intro.mp4' } });
    viewer.app.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    viewer.on('videoReady', attachScreen);
    const timeout = setTimeout(() => fail('模型加载超时，请检查本地服务，然后重新加载。'), 30000);
    try { await viewer.load({ basePath: './', crossOrigin: 'anonymous' }); }
    finally { clearTimeout(timeout); }
    if (failed) return;
    loaded = true;
    viewer.setRotation(0, 0);
    viewer.start();
    viewer.enableControls();
    interactions = new DeviceInteractions(viewer, canvas, preference);
    document.querySelector('#loading').hidden = true;
    if (video.readyState >= HTMLMediaElement.HAVE_FUTURE_DATA) attachScreen();
    if (!preference.matches) viewer.animateIdle();
  } catch (error) {
    console.error(error);
    fail('无法显示模型。请通过本地服务打开，并确认浏览器已开启 WebGL。');
  }
}

canvas.addEventListener('keydown', event => {
  if (!loaded || failed || !['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home'].includes(event.key)) return;
  event.preventDefault();
  stopMotion();
  if (event.key === 'Home') { viewer.setRotation(0, 0); return; }
  const rotation = viewer.app.device.rotation;
  let x = THREE.Math.radToDeg(rotation.x), y = THREE.Math.radToDeg(rotation.y);
  if (event.key === 'ArrowLeft') y -= 12;
  if (event.key === 'ArrowRight') y += 12;
  if (event.key === 'ArrowUp') x -= 12;
  if (event.key === 'ArrowDown') x += 12;
  viewer.setRotation(x, y);
});
canvas.addEventListener('pointerdown', () => { if (screenReady && video.paused && !preference.matches) void playScreen(); });
document.querySelector('#retry').addEventListener('click', () => window.location.reload());
video.addEventListener('error', () => { if (loaded) viewer.showDefaultScreen(); });
canvas.addEventListener('webglcontextlost', event => { event.preventDefault(); fail('图形上下文已中断，请重新加载。'); });
new ResizeObserver(() => { if (viewer) { const box = container.getBoundingClientRect(); viewer.setSize(box.width, box.height); interactions?.resize(); } }).observe(container);
preference.addEventListener('change', event => { if (event.matches && loaded) { stopMotion(); video.pause(); } });
void initialize();
