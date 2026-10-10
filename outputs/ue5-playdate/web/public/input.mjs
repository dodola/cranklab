export const BUTTONS = Object.freeze({ left: 1, right: 2, up: 4, down: 8, B: 16, A: 32 });
export const KEY_BUTTONS = Object.freeze({ ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down', a: 'left', d: 'right', w: 'up', s: 'down', z: 'A', x: 'B' });
export class HeldButtons {
  constructor(changed) { this.sources = new Map(); this.changed = changed; this.mask = 0; }
  set(source, button) {
    if (button) this.sources.set(source, button); else this.sources.delete(source);
    let mask = 0;
    for (const value of this.sources.values()) mask |= BUTTONS[value] || 0;
    if (this.mask !== mask) { this.mask = mask; this.changed(mask); }
  }
  releaseAll() { this.sources.clear(); if (this.mask) { this.mask = 0; this.changed(0); } }
}
export function angleDelta(previous, current) {
  let difference = current - previous;
  while (difference > 180) difference -= 360;
  while (difference < -180) difference += 360;
  return difference;
}
