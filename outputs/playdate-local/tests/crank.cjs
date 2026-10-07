'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../interactions.js'), 'utf8');

function fixture() {
  const canvasEvents = {}, windowEvents = {}, documentEvents = {};
  const environment = {
    window: {addEventListener: (name, callback) => { windowEvents[name] = callback; }},
    document: {hidden:false,addEventListener:(name,callback) => { documentEvents[name] = callback; }},
    CustomEvent: class { constructor(type, options) { this.type = type; this.detail = options.detail; } }
  };
  vm.runInNewContext(source + '\nthis.Interactions = DeviceInteractions;', environment);
  const input = Object.create(environment.Interactions.prototype);
  Object.assign(input, {
    canvas:{addEventListener:(name,callback) => { canvasEvents[name] = callback; },dispatchEvent(){}},
    viewer:{app:{controllers:{crank:{setRotationAngle(){}}}}},
    pressed:new Set(), parts:{}, crankKeys:new Set(), crankAngle:0, targetCrankAngle:0, crankVelocity:0, crankOpen:false,
    stopIdle(){}, prepareCrank(){}, toggleCrank(){this.crankOpen = !this.crankOpen;}
  });
  input.bind();
  const key = (type, value, repeat = false) => canvasEvents[type]({key:value,repeat,preventDefault(){},stopImmediatePropagation(){}});
  const frames = (seconds, fps = 60) => { for (let i=0;i<seconds*fps;i++) input.updateCrank(1/fps); };
  return {input,key,frames,blur:windowEvents.blur};
}

const forward = fixture();
forward.key('keydown','e');
assert.equal(forward.input.crankOpen,true,'E unfolds a closed crank');
forward.frames(1);
const first = forward.input.crankAngle;
assert.ok(first>220 && first<300,'Holding E turns continuously without key repeat');
forward.key('keydown','e',true);
forward.frames(1);
assert.ok(forward.input.crankAngle-first>290,'Repeat keydown does not reset speed');
forward.key('keyup','e');
const released = forward.input.crankAngle;
forward.frames(1);
assert.ok(forward.input.crankAngle-released<50,'Release smoothly stops, rather than running forever');
const settled = forward.input.crankAngle;
forward.frames(1);
assert.ok(Math.abs(forward.input.crankAngle-settled)<.02,'Crank settles after release');
forward.key('keydown','q');
forward.frames(1);
assert.ok(forward.input.crankAngle<settled-220,'Holding Q turns in reverse');
forward.blur();
const lostFocus = forward.input.crankAngle;
forward.frames(2);
assert.equal(forward.input.crankAngle,lostFocus,'Blur clears held keys and motion');
assert.equal(forward.input.crankKeys.size,0);

const angles = [30,60,144].map(fps => { const f=fixture(); f.key('keydown','e'); f.frames(2,fps); return f.input.crankAngle; });
assert.ok(Math.max(...angles)-Math.min(...angles)<8,'Speed is consistent across frame rates');
console.log('Passed: continuous E/Q, repeat, release, reverse, blur and 30/60/144 FPS consistency.');
