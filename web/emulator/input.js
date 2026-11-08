// Button bits for the GBA KEYINPUT register and the input state machine.
// A tap that is pressed and released within one display frame is still held for
// at least one emulated frame via `pendingPresses`.

export const KEYINPUT = 0x04000130;
export const KEY_MASK = 0x03ff;

export const BUTTONS = Object.freeze({
  a: 1 << 0,
  b: 1 << 1,
  select: 1 << 2,
  start: 1 << 3,
  right: 1 << 4,
  left: 1 << 5,
  up: 1 << 6,
  down: 1 << 7,
  r: 1 << 8,
  l: 1 << 9,
});

export const BUTTON_NAMES = Object.freeze(Object.keys(BUTTONS));

export const DEFAULT_KEY_MAP = new Map([
  ['KeyE', 'a'], ['KeyF', 'b'], ['KeyZ', 'select'], ['KeyX', 'start'],
  ['ArrowRight', 'right'], ['ArrowLeft', 'left'], ['ArrowUp', 'up'], ['ArrowDown', 'down'],
  ['KeyD', 'right'], ['KeyA', 'left'], ['KeyW', 'up'], ['KeyS', 'down'],
  ['KeyR', 'r'], ['KeyQ', 'l'],
]);

export function createInputState() {
  const pressed = new Set();
  const pendingPresses = new Map();
  const listeners = new Set();

  function notify(name, isPressed) {
    for (const fn of listeners) fn(name, isPressed);
  }

  return {
    /** Bitmask of everything currently held (including minimum-duration taps). */
    heldMask() {
      let held = 0;
      for (const key of pressed) held |= BUTTONS[key] || 0;
      for (const key of pendingPresses.keys()) held |= BUTTONS[key] || 0;
      return held;
    },
    /** Human input: guarantees at least one emulated frame of "held". */
    setPressed(name, isPressed) {
      if (!Object.hasOwn(BUTTONS, name)) throw new Error(`unknown button: ${name}`);
      if (isPressed) {
        pressed.add(name);
        pendingPresses.set(name, 1);
      } else {
        pressed.delete(name);
      }
      notify(name, isPressed);
    },
    /** Automation input: exact, no minimum-duration extension. */
    setHeld(name, isPressed) {
      if (!Object.hasOwn(BUTTONS, name)) throw new Error(`unknown button: ${name}`);
      pendingPresses.delete(name);
      if (isPressed) pressed.add(name);
      else pressed.delete(name);
      notify(name, isPressed);
    },
    /** Call once per emulated frame. */
    step() {
      for (const [key, frames] of pendingPresses) {
        if (frames <= 1) pendingPresses.delete(key);
        else pendingPresses.set(key, frames - 1);
      }
    },
    clear() {
      pressed.clear();
      pendingPresses.clear();
    },
    isPressed(name) {
      return pressed.has(name);
    },
    onChange(fn) {
      listeners.add(fn);
      return () => listeners.delete(fn);
    },
  };
}
