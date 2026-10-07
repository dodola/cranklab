'use strict';

// Uses the original geometry/materials; only the interactive material groups
// become independent meshes. No invisible approximation replaces the buttons.
class DeviceInteractions {
  constructor(viewer, canvas, reducedMotion) {
    this.viewer = viewer;
    this.canvas = canvas;
    this.reducedMotion = reducedMotion;
    this.parts = {};
    this.pressed = new Set();
    // The site's reduced Three.js bundle omits Raycaster/Ray methods.
    // Use a self-contained Moller-Trumbore triangle intersection instead.
    this.ray = {origin:new THREE.Vector3(),direction:new THREE.Vector3()};
    this.pointer = new THREE.Vector2();
    this.crankOpen = false;
    this.crankAngle = 0;
    this.targetCrankAngle = 0;
    this.crankVelocity = 0;
    this.crankKeys = new Set();
    this.status = document.querySelector('#input-status');
    this.splitButtons();
    this.crank = viewer.app.scene.getObjectByName('crank');
    this.arm = viewer.app.scene.getObjectByName('arm_pivot');
    this.handle = viewer.app.scene.getObjectByName('handle_pivot');
    this.axle = viewer.app.scene.getObjectByName('axle');
    this.folded = { arm: this.arm.rotation.y, handle: this.handle.rotation.x };
    this.crankMeshes = [];
    this.crank.traverse(node => { if (node.isMesh && node.name !== 'crank_shadow') this.crankMeshes.push(node); });
    this.bind();
    this.resize();
    // Update through the viewer's own frame loop, before it renders. Key hold
    // speed and damping use elapsed seconds rather than OS key-repeat timing.
    const update = viewer.app.update.bind(viewer.app);
    viewer.app.update = frame => { this.updateCrank(frame.delta); update(frame); };
  }

  zoomFor(open = this.crankOpen) {
    const box = this.canvas.getBoundingClientRect();
    const fit = box.width / (box.height * .51 * (open ? 1.34 : 1.06)) * .94;
    return Math.min(open ? 1.28 : 1.45, fit);
  }
  resize() {
    this.viewer.app.camera.zoom = this.zoomFor();
    this.viewer.app.camera.updateProjectionMatrix();
  }
  updateCrank(delta) {
    if (!this.crankOpen) return;
    const dt = Math.min(Math.max(delta, 0), .05);
    const direction = Number(this.crankKeys.has('e')) - Number(this.crankKeys.has('q'));
    this.crankVelocity += (direction * 300 - this.crankVelocity) * (1 - Math.exp(-12 * dt));
    this.targetCrankAngle += this.crankVelocity * dt;
    const step = (this.targetCrankAngle - this.crankAngle) * (1 - Math.exp(-22 * dt));
    if (Math.abs(step) < .0001) return;
    this.crankAngle += step;
    this.viewer.app.controllers.crank.setRotationAngle(this.crankAngle);
    this.canvas.dispatchEvent(new CustomEvent('playdate-input', {detail:{type:'crank',angle:this.crankAngle,delta:step}}));
  }

  splitButtons() {
    const main = this.viewer.app.scene.getObjectByName('main');
    const geometry = main.geometry;
    const indices = geometry.index.array;
    const positions = geometry.attributes.position.array;
    const groups = geometry.groups;
    const buckets = { A: [], B: [], menu: [], sleep: [], dpad: [] };
    for (const group of groups.filter(group => [1, 6].includes(group.materialIndex))) {
      for (let i = group.start; i < group.start + group.count; i += 3) {
        const triangle = [indices[i], indices[i + 1], indices[i + 2]];
        const x = triangle.reduce((sum, index) => sum + positions[index * 3], 0) / 3;
        const z = triangle.reduce((sum, index) => sum + positions[index * 3 + 2], 0) / 3;
        const name = group.materialIndex === 6 ? 'dpad' : z < 0 ? (x < -.15 ? 'A' : 'B') : z > .35 ? 'sleep' : 'menu';
        buckets[name].push(...triangle);
      }
    }
    for (const [name, indexList] of Object.entries(buckets)) {
      const partGeometry = new THREE.BufferGeometry();
      for (const [attribute, data] of Object.entries(geometry.attributes)) partGeometry.addAttribute(attribute, data.clone());
      partGeometry.setIndex(new THREE.BufferAttribute(new Uint32Array(indexList), 1));
      // BufferGeometry bounds include unused vertices, so derive part bounds
      // from its indexed vertices before centering the independent mesh.
      const bounds = new THREE.Box3();
      for (const index of indexList) bounds.expandByPoint(new THREE.Vector3().fromArray(positions, index * 3));
      const center = bounds.getCenter(new THREE.Vector3());
      partGeometry.translate(-center.x, -center.y, -center.z);
      partGeometry.boundingBox = bounds.clone().translate(center.clone().negate());
      partGeometry.boundingSphere = partGeometry.boundingBox.getBoundingSphere(new THREE.Sphere());
      const material = main.material[name === 'dpad' ? 6 : 1];
      const mesh = new THREE.Mesh(partGeometry, material);
      mesh.name = `input-${name}`;
      mesh.position.copy(center);
      mesh.userData.rest = center.clone();
      mesh.userData.input = name;
      main.add(mesh);
      this.parts[name] = mesh;
    }
    main.geometry = geometry.clone();
    main.geometry.groups = groups.filter(group => ![1, 6].includes(group.materialIndex)).map(group => ({ ...group }));
    this.main = main;
  }

  announce(text) { this.status.textContent = text; }
  stopIdle() {
    this.viewer.app.isIdleAnimating = false;
    this.viewer.app.controls.setOrientationFromObject(this.viewer.app.device);
  }
  pick(event) {
    const bounds = this.canvas.getBoundingClientRect();
    this.pointer.set((event.clientX - bounds.left) / bounds.width * 2 - 1, -(event.clientY - bounds.top) / bounds.height * 2 + 1);
    this.viewer.app.scene.updateMatrixWorld(true);
    const origin = this.viewer.app.camera.getWorldPosition(new THREE.Vector3());
    const direction = new THREE.Vector3(this.pointer.x, this.pointer.y, .5).unproject(this.viewer.app.camera).sub(origin).normalize();
    this.ray.origin.copy(origin);
    this.ray.direction.copy(direction);
    // Include the body so the rear casing correctly occludes front buttons.
    const hit = this.intersectMeshes([this.main, ...Object.values(this.parts), ...this.crankMeshes]);
    if (!hit) return null;
    if (hit.object.userData.input) return { kind: 'button', mesh: hit.object, point: hit.point };
    if (this.crankMeshes.includes(hit.object)) return { kind: 'crank', point: hit.point };
    return null;
  }

  intersectMeshes(meshes) {
    let nearest = null;
    const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3();
    const edge1 = new THREE.Vector3(), edge2 = new THREE.Vector3(), h = new THREE.Vector3(), s = new THREE.Vector3(), q = new THREE.Vector3();
    for (const mesh of meshes) {
      if (!mesh.visible) continue;
      const geometry = mesh.geometry;
      const inverse = new THREE.Matrix4().getInverse(mesh.matrixWorld);
      const localOrigin = this.ray.origin.clone().applyMatrix4(inverse);
      const localDirection = this.ray.direction.clone().transformDirection(inverse);
      const groups = geometry.groups.length ? geometry.groups : [{start:0,count:geometry.index.count,materialIndex:0}];
      for (const group of groups) {
        const material = Array.isArray(mesh.material) ? mesh.material[group.materialIndex] : mesh.material;
        if (!material || material.visible === false) continue;
        for (let i = group.start; i < group.start + group.count; i += 3) {
          a.fromBufferAttribute(geometry.attributes.position, geometry.index.getX(i));
          b.fromBufferAttribute(geometry.attributes.position, geometry.index.getX(i + 1));
          c.fromBufferAttribute(geometry.attributes.position, geometry.index.getX(i + 2));
          edge1.subVectors(b,a); edge2.subVectors(c,a);
          h.crossVectors(localDirection,edge2);
          const det = edge1.dot(h);
          if (Math.abs(det) < 1e-9 || (material.side === THREE.FrontSide && det < 0) || (material.side === THREE.BackSide && det > 0)) continue;
          s.subVectors(localOrigin,a);
          const u = s.dot(h) / det;
          if (u < 0 || u > 1) continue;
          q.crossVectors(s,edge1);
          const v = localDirection.dot(q) / det;
          if (v < 0 || u + v > 1) continue;
          const t = edge2.dot(q) / det;
          if (t < 0) continue;
          const worldPoint = localDirection.clone().multiplyScalar(t).add(localOrigin).applyMatrix4(mesh.matrixWorld);
          const distance = worldPoint.distanceTo(this.ray.origin);
          if (!nearest || distance < nearest.distance) nearest = {object:mesh,point:worldPoint,distance};
        }
      }
    }
    return nearest;
  }

  press(mesh, direction) {
    this.stopIdle();
    mesh.userData.releaseTween?.stop();
    mesh.userData.releaseRotationTween?.stop();
    this.pressed.add(mesh);
    mesh.userData.pressedInput = direction || mesh.userData.input;
    mesh.position.copy(mesh.userData.rest);
    if (mesh.userData.input === 'sleep') mesh.position.z -= .004;
    else mesh.position.y -= .009;
    if (mesh.userData.input === 'dpad') {
      mesh.rotation.set(0, 0, 0);
      if (direction === 'up') mesh.rotation.x = -.065;
      if (direction === 'down') mesh.rotation.x = .065;
      if (direction === 'left') mesh.rotation.z = -.065;
      if (direction === 'right') mesh.rotation.z = .065;
    }
    this.announce(`${{A:'A 键',B:'B 键',menu:'菜单键',sleep:'顶部按键',dpad:'方向键'}[mesh.userData.input]}${direction ? ' ' + {up:'上',down:'下',left:'左',right:'右'}[direction] : ''}已按下`);
    this.canvas.dispatchEvent(new CustomEvent('playdate-input', { detail: { type: 'button', button: direction || mesh.userData.input, pressed: true } }));
  }
  release(mesh) {
    if (!mesh || !this.pressed.has(mesh)) return;
    this.pressed.delete(mesh);
    const duration = this.reducedMotion.matches ? 0 : 140;
    const rest = mesh.userData.rest;
    mesh.userData.releaseTween = new TWEEN.Tween(mesh.position).to({x:rest.x,y:rest.y,z:rest.z}, duration).easing(TWEEN.Easing.Quadratic.Out).start();
    mesh.userData.releaseRotationTween = new TWEEN.Tween(mesh.rotation).to({x:0,y:0,z:0}, duration).start();
    this.canvas.dispatchEvent(new CustomEvent('playdate-input', { detail: { type: 'button', button: mesh.userData.pressedInput, pressed: false } }));
  }
  direction(hit) {
    const point = hit.mesh.worldToLocal(hit.point.clone());
    return Math.abs(point.x) > Math.abs(point.z) ? (point.x > 0 ? 'left' : 'right') : (point.z > 0 ? 'up' : 'down');
  }

  toggleCrank() {
    this.stopIdle();
    this.foldTween?.stop();
    this.crankOpen = !this.crankOpen;
    this.crankKeys.clear();
    this.crankVelocity = 0;
    // Start folding from a neutral rotation to prevent clipping the shell.
    this.crankAngle = this.targetCrankAngle = 0;
    this.viewer.app.controllers.crank.setRotationAngle(0);
    const state = { arm: this.arm.rotation.y, handle: this.handle.rotation.x, zoom: this.viewer.app.camera.zoom, x: this.viewer.app.device.position.x };
    const target = this.crankOpen ? {arm:Math.PI,handle:Math.PI/2,zoom:this.zoomFor(true),x:.06} : {...this.folded,zoom:this.zoomFor(false),x:0};
    this.foldTween = new TWEEN.Tween(state).to(target, this.reducedMotion.matches ? 0 : 450).easing(TWEEN.Easing.Quadratic.InOut).onUpdate(() => {
      this.arm.rotation.y = state.arm;
      this.handle.rotation.x = state.handle;
      this.viewer.app.camera.zoom = state.zoom;
      this.viewer.app.camera.updateProjectionMatrix();
      this.viewer.app.device.position.x = state.x;
    }).start();
    this.announce(this.crankOpen ? '摇柄已展开，拖动手柄旋转；双击收起' : '摇柄已收起');
  }
  prepareCrank() {
    if (this.crankOpen) {
      this.foldTween?.stop();
      this.arm.rotation.y = Math.PI;
      this.viewer.app.camera.zoom = this.zoomFor(true);
      this.viewer.app.camera.updateProjectionMatrix();
      this.viewer.app.device.position.x = .06;
    }
  }
  turnCrank(delta) {
    this.prepareCrank();
    this.targetCrankAngle += delta;
  }

  bind() {
    this.canvas.addEventListener('pointerdown', event => {
      if (event.button !== 0 || this.active) return;
      const hit = this.pick(event);
      if (!hit) return;
      event.preventDefault();
      event.stopImmediatePropagation();
      this.stopIdle();
      this.viewer.disableControls();
      this.viewer.app.controls.isMouseDown = false;
      this.canvas.setPointerCapture(event.pointerId);
      this.active = { ...hit, id: event.pointerId, x: event.clientX, y: event.clientY, moved: false };
      if (hit.kind === 'button') this.press(hit.mesh, hit.mesh.userData.input === 'dpad' ? this.direction(hit) : null);
      else if (!this.crankOpen) { this.toggleCrank(); this.active.justOpened = true; }
    }, true);
    for (const type of ['mousedown', 'touchstart']) this.canvas.addEventListener(type, event => {
      if (this.active) { event.preventDefault(); event.stopImmediatePropagation(); }
    }, {capture:true, passive:false});
    this.canvas.addEventListener('pointermove', event => {
      if (!this.active) { this.canvas.style.cursor = this.pick(event) ? 'pointer' : 'grab'; return; }
      if (event.pointerId !== this.active.id) return;
      const dx = event.clientX - this.active.x, dy = event.clientY - this.active.y;
      this.active.moved ||= Math.abs(dx) + Math.abs(dy) > 3;
      if (this.active.kind === 'crank' && this.crankOpen && !this.active.justOpened) {
        this.foldTween?.stop();
        // Vertical movement maps to the shaft's X axis, which appears edge-on
        // from the front; horizontal movement contributes for angled views.
        this.turnCrank(dy * 1.2 + dx * .35);
      }
      this.active.x = event.clientX;
      this.active.y = event.clientY;
    });
    const finish = event => {
      if (!this.active || (event.pointerId !== undefined && event.pointerId !== this.active.id)) return;
      if (this.active.kind === 'button') this.release(this.active.mesh);
      else if (event.type === 'pointerup' && !this.active.moved && !this.active.justOpened) this.turnCrank(45);
      const id = this.active.id;
      this.active = null;
      if (this.canvas.hasPointerCapture(id)) this.canvas.releasePointerCapture(id);
      this.viewer.app.controls.isMouseDown = false;
      this.viewer.app.controls.isRotating = false;
      this.viewer.enableControls();
    };
    for (const type of ['pointerup','pointercancel','lostpointercapture']) this.canvas.addEventListener(type, finish);
    this.canvas.addEventListener('dblclick', event => { if (this.pick(event)?.kind === 'crank' && this.crankOpen) { event.preventDefault(); this.toggleCrank(); } });
    this.canvas.addEventListener('wheel', event => {
      if (this.pick(event)?.kind !== 'crank') return;
      event.preventDefault();
      this.stopIdle();
      if (!this.crankOpen) this.toggleCrank(); else this.turnCrank(event.deltaY * .3);
    }, {passive:false});
    const keys = {z:'A',x:'B',w:'up',a:'left',s:'down',d:'right',m:'menu'};
    this.canvas.addEventListener('keydown', event => {
      const key = event.key.toLowerCase();
      if (!keys[key] && !['c','q','e'].includes(key)) return;
      event.preventDefault(); event.stopImmediatePropagation();
      if (key === 'q' || key === 'e') {
        this.stopIdle();
        if (!this.crankOpen) this.toggleCrank();
        this.prepareCrank();
        if (!this.crankKeys.has(key)) this.targetCrankAngle += key === 'q' ? -12 : 12;
        this.crankKeys.add(key);
        return;
      }
      if (event.repeat) return;
      if (key === 'c') this.toggleCrank();
      else this.press(this.parts[keys[key]] || this.parts.dpad, this.parts[keys[key]] ? null : keys[key]);
    }, true);
    this.canvas.addEventListener('keyup', event => {
      this.crankKeys.delete(event.key.toLowerCase());
      const key = keys[event.key.toLowerCase()];
      if (key) this.release(this.parts[key] || this.parts.dpad);
    });
    const cleanup = () => {
      this.crankKeys.clear(); this.crankVelocity = 0; this.targetCrankAngle = this.crankAngle;
      finish({}); for (const mesh of this.pressed) this.release(mesh);
    };
    window.addEventListener('blur', cleanup);
    document.addEventListener('visibilitychange', () => { if (document.hidden) cleanup(); });
    this.canvas.addEventListener('blur', cleanup);
  }
}
