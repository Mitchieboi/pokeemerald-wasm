// pokeemerald-wasm emulator library.
//
//   const module = await loadGameModule('/build/wasm/pokeemerald.wasm');
//   const emu = await createEmulator({ canvas, module, flash, workletUrl, onFlashChanged });
//   emu.start();
//
// The wasm module uses GBA addresses as wasm addresses (fixed 256 MiB memory), so
// everything below talks to the game through shared memory: KEYINPUT for input,
// the flash region for saves, and exported globals/functions for everything else.

import { createBios } from './bios.js';
import { createAudio } from './audio.js';
import { createInputState, KEYINPUT, KEY_MASK, BUTTONS } from './input.js';
import { FLASH_BASE, FLASH_SIZE, hashBytes, normalizeSaveForCurrentBuild, SAVE_BLOCK2_TRAINER_ID_OFFSET } from './save.js';

export { BUTTONS, BUTTON_NAMES, DEFAULT_KEY_MAP } from './input.js';
export { FLASH_SIZE, blankFlash, isValidSaveImage, normalizeSaveForCurrentBuild } from './save.js';

export const WIDTH = 240;
export const HEIGHT = 160;
const MIN_SPEED = 0.1;
const MAX_SPEED = 1000;
const FAST_FRAME_BUDGET_MS = 16;
const SAVE_FLUSH_INTERVAL_MS = 1000;

const moduleCache = new Map();

/** Compile (and cache per URL) the game module. Uses streaming compile when the server sends application/wasm. */
export function loadGameModule(wasmUrl, { cache = 'default' } = {}) {
  if (!moduleCache.has(wasmUrl)) {
    const promise = (async () => {
      const response = await fetch(wasmUrl, { cache });
      if (!response.ok) throw new Error(`failed to fetch ${wasmUrl}: ${response.status}`);
      const type = response.headers.get('content-type') || '';
      if (type.includes('application/wasm') && WebAssembly.compileStreaming) {
        return WebAssembly.compileStreaming(response);
      }
      return WebAssembly.compile(await response.arrayBuffer());
    })();
    promise.catch(() => moduleCache.delete(wasmUrl));
    moduleCache.set(wasmUrl, promise);
  }
  return moduleCache.get(wasmUrl);
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

/**
 * @param {object} opts
 * @param {HTMLCanvasElement} opts.canvas 240x160 canvas to draw into
 * @param {WebAssembly.Module} opts.module compiled game module
 * @param {Uint8Array} [opts.flash] 128 KiB flash image to boot from (default: blank)
 * @param {Uint8Array} [opts.modConfig] bytes written to the exported `gModConfig` before AgbMain()
 * @param {string} [opts.workletUrl] URL of audio-worklet.js (omit to run silent)
 * @param {number} [opts.speed=1] initial speed multiplier; Infinity = unlimited
 * @param {(flash: Uint8Array) => void} [opts.onFlashChanged] called with a copy when the flash image changed
 * @param {(status: {displayFps:number, gameFps:number}) => void} [opts.onStatus]
 * @param {(error: Error) => void} [opts.onError]
 */
export async function createEmulator(opts) {
  const {
    canvas, module, workletUrl,
    onFlashChanged = null, onStatus = null, onError = null,
  } = opts;
  if (!canvas || !module) throw new Error('createEmulator: canvas and module are required');

  const ctx2d = canvas.getContext('2d');
  const input = createInputState();
  const audio = createAudio({ workletUrl });

  let instance = null;
  let memory = null;
  let views = null;
  let bios = null;
  let image = null;
  let running = false;
  let destroyed = false;
  let rafId = 0;
  let speed = 1;
  let lastFiniteSpeed = 1;
  let currentFrame = 0;
  let gameFrameAccumulator = 0;
  let lastTick = 0;
  let lastFpsUpdate = 0;
  let renderedFrames = 0;
  let emulatedFrames = 0;
  let lastSavedFlashHash = 0;
  let lastSaveFlushTime = 0;

  function fail(error) {
    console.error(error);
    if (onError) onError(error);
  }

  function flashBytes() {
    return views.u8.subarray(FLASH_BASE, FLASH_BASE + FLASH_SIZE);
  }

  function writeKeys(mask = input.heldMask()) {
    views.u16[KEYINPUT >> 1] = KEY_MASK ^ mask;
  }

  function render() {
    instance.exports.WasmRenderFrame();
    ctx2d.putImageData(image, 0, 0);
  }

  function checkFlash(force = false) {
    if (!views) return false;
    const now = performance.now();
    if (!force && now - lastSaveFlushTime < SAVE_FLUSH_INTERVAL_MS) return false;
    lastSaveFlushTime = now;
    const flash = flashBytes();
    const hash = hashBytes(flash);
    if (!force && hash === lastSavedFlashHash) return false;
    const changed = hash !== lastSavedFlashHash;
    lastSavedFlashHash = hash;
    if (changed && onFlashChanged) onFlashChanged(new Uint8Array(flash));
    return changed;
  }

  function runFrames(frameCount, keyMask = 0) {
    for (let i = 0; i < frameCount; i++) {
      if (keyMask) views.u16[KEYINPUT >> 1] = KEY_MASK ^ keyMask;
      else writeKeys();
      instance.exports.WasmRunFrame();
      audio.feed(instance, memory, bios.readS32);
      currentFrame++;
      input.step();
    }
    views.u16[KEYINPUT >> 1] = KEY_MASK;
    checkFlash();
  }

  function runFramesForTick(elapsedMs) {
    const start = performance.now();
    const frameBudgetMs = speed === Infinity ? elapsedMs : FAST_FRAME_BUDGET_MS;
    let frameCount;
    if (speed === Infinity) {
      gameFrameAccumulator = 0;
      frameCount = Infinity;
    } else {
      gameFrameAccumulator += speed * elapsedMs / (1000 / 60);
      frameCount = Math.floor(gameFrameAccumulator);
      gameFrameAccumulator -= frameCount;
      if (frameCount === 0) return 0;
    }
    let frames = 0;
    while (frames < frameCount && performance.now() - start < frameBudgetMs) {
      const batchSize = Math.min(frameCount - frames, 256);
      runFrames(batchSize);
      frames += batchSize;
    }
    return frames;
  }

  function updateFps(frameCount) {
    renderedFrames++;
    emulatedFrames += frameCount;
    const now = performance.now();
    const elapsed = now - lastFpsUpdate;
    if (elapsed < 1000) return;
    if (onStatus) {
      onStatus({
        displayFps: Math.round(renderedFrames * 1000 / elapsed),
        gameFps: Math.round(emulatedFrames * 1000 / elapsed),
      });
    }
    lastFpsUpdate = now;
    renderedFrames = 0;
    emulatedFrames = 0;
  }

  function tick(now) {
    if (!running) return;
    try {
      const elapsedMs = Math.min(now - lastTick, 100);
      lastTick = now;
      const frames = runFramesForTick(elapsedMs);
      render();
      updateFps(frames);
      rafId = requestAnimationFrame(tick);
    } catch (error) {
      running = false;
      fail(error);
    }
  }

  async function boot() {
    const bootViews = {};
    bios = createBios(bootViews);
    instance = await WebAssembly.instantiate(module, bios.importsFor(module));
    memory = instance.exports.memory;
    bootViews.u8 = new Uint8Array(memory.buffer);
    bootViews.u16 = new Uint16Array(memory.buffer);
    bootViews.u32 = new Uint32Array(memory.buffer);
    views = bootViews;

    const ptr = instance.exports.WasmDisplayBuffer();
    const size = instance.exports.WasmDisplayBufferSize();
    image = new ImageData(new Uint8ClampedArray(memory.buffer, ptr, size), WIDTH, HEIGHT);

    // Flash: boot from the supplied image (normalised to this build's layout) or a blank chip.
    const flash = flashBytes();
    flash.fill(0xff);
    if (opts.flash?.length === FLASH_SIZE) {
      flash.set(normalizeSaveForCurrentBuild(opts.flash) ?? opts.flash);
    }
    lastSavedFlashHash = hashBytes(flash);
    lastSaveFlushTime = performance.now();

    // Mod configuration, if this build exports it.
    if (opts.modConfig) {
      const target = instance.exports.gModConfig;
      if (!target) console.warn('emulator: modConfig given but the build has no gModConfig export');
      else views.u8.set(opts.modConfig, target.value);
    }

    writeKeys();
    instance.exports.AgbMain();
    currentFrame = 0;
    gameFrameAccumulator = 0;
    render();
  }

  function setSpeed(value) {
    speed = value === Infinity ? Infinity : clamp(Number(value) || 1, MIN_SPEED, MAX_SPEED);
    if (speed !== Infinity) lastFiniteSpeed = speed;
    audio.setEnabled(speed === 1);
    gameFrameAccumulator = 0;
  }

  await boot();
  setSpeed(opts.speed ?? 1);

  const api = {
    get instance() { return instance; },
    get memory() { return memory; },
    get views() { return views; },
    get frame() { return currentFrame; },
    get running() { return running; },
    audio,
    input,

    start() {
      if (destroyed || running) return;
      running = true;
      lastTick = performance.now();
      lastFpsUpdate = lastTick;
      renderedFrames = 0;
      emulatedFrames = 0;
      rafId = requestAnimationFrame(tick);
    },
    stop() {
      running = false;
      if (rafId) cancelAnimationFrame(rafId);
      rafId = 0;
      checkFlash(true);
    },
    async destroy() {
      api.stop();
      destroyed = true;
      await audio.close();
      instance = null;
      memory = null;
      views = null;
    },

    get speed() { return speed; },
    setSpeed,
    toggleUnlimitedSpeed() { setSpeed(speed === Infinity ? lastFiniteSpeed : Infinity); },

    /** Human input (minimum one-frame hold). */
    setButton(name, isPressed) {
      input.setPressed(name, isPressed);
      if (views) writeKeys();
    },
    /** Exact input for automation. */
    setButtonHeld(name, isPressed) {
      input.setHeld(name, isPressed);
      if (views) writeKeys();
    },

    /** Run exactly `n` frames now (automation / tests). */
    runFrames(n, keyMask = 0) {
      runFrames(n, keyMask);
    },
    runToFrame(target) {
      if (!Number.isInteger(target) || target < currentFrame) {
        throw new Error(`cannot run from frame ${currentFrame} to ${target}`);
      }
      runFrames(target - currentFrame);
      render();
      return currentFrame;
    },
    render,

    /** Copy of the current flash image. */
    getFlash() { return new Uint8Array(flashBytes()); },
    /** Force a flash check; returns true if it changed since the last report. */
    flushSave() { return checkFlash(true); },
    flashHash() { return hashBytes(flashBytes()); },

    screenshot(type = 'image/png') { return canvas.toDataURL(type); },

    /** Trainer id from SaveBlock2 — handy to tell a fresh game from a loaded one. */
    trainerId() {
      const sb2 = views.u32[instance.exports.gSaveBlock2Ptr.value >> 2];
      return views.u16[(sb2 + SAVE_BLOCK2_TRAINER_ID_OFFSET) >> 1];
    },
    readU32(ptr) { return views.u32[ptr >> 2]; },
    readU16(ptr) { return views.u16[ptr >> 1]; },
    readU8(ptr) { return views.u8[ptr]; },
    readS16: (ptr) => bios.readS16(ptr),
    readS32: (ptr) => bios.readS32(ptr),
  };
  return api;
}
