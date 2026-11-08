// Upstream dev page: binds web/index.html to the emulator library (web/emulator/).
// Saves persist in localStorage; `?speed=N` and `?automate=1` behave as before.
import { createEmulator, loadGameModule, FLASH_SIZE, DEFAULT_KEY_MAP, BUTTONS, normalizeSaveForCurrentBuild } from './emulator/emulator.js';
import { SAVE_BLOCK2_TRAINER_ID_OFFSET } from './emulator/save.js';

const WASM_URL = '/build/wasm/pokeemerald.wasm';
const WORKLET_URL = '/web/emulator/audio-worklet.js';
const SAVE_STORAGE_KEY = 'pokeemerald.wasm.flash.v1';
const HEIGHT = 160;
const REG = 0x04000000;
const REG_OFFSET_WIN0H = 0x40;
const REG_OFFSET_WIN0V = 0x44;
const REG_OFFSET_WININ = 0x48;
const REG_OFFSET_WINOUT = 0x4a;
const REG_OFFSET_DMA0 = 0xb0;
const DMA_DEST_FIXED = 0x0040;
const DMA_DEST_RELOAD = 0x0060;
const DMA_REPEAT = 0x0200;
const DMA_START_HBLANK = 0x2000;
const DMA_ENABLE = 0x8000;

const searchParams = new URLSearchParams(location.search);
const speedParam = searchParams.get('speed');
const automate = searchParams.get('automate') === '1';

const canvas = document.querySelector('#screen');
const statusEl = document.querySelector('#status');
const speedButtons = document.querySelectorAll('[data-speed]');
const speedValue = document.querySelector('#speed-value');
const fullscreenButton = document.querySelector('#fullscreen');
const shell = document.querySelector('.shell');
const downloadSaveButton = document.querySelector('#download-save');
const uploadSaveInput = document.querySelector('#upload-save');

let emu = null;
let statusText = 'loading wasm…';
let wasmBytes = 0;
let automationReady;
let resolveAutomationReady;
if (automate) automationReady = new Promise((resolve) => { resolveAutomationReady = resolve; });

function bytesToBase64(bytes) {
  let binary = '';
  for (let i = 0; i < bytes.length; i += 0x8000) binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(binary);
}

function base64ToBytes(base64) {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

function storedFlash() {
  try {
    const stored = localStorage.getItem(SAVE_STORAGE_KEY);
    if (!stored) return null;
    const saved = base64ToBytes(stored);
    return saved.length === FLASH_SIZE ? saved : null;
  } catch {
    return null;
  }
}

function storeFlash(bytes) {
  try {
    localStorage.setItem(SAVE_STORAGE_KEY, bytesToBase64(bytes));
  } catch {
    // Storage may be disabled; the game can still run with volatile flash.
  }
}

function downloadSave() {
  if (!emu) return;
  emu.flushSave();
  const blob = new Blob([emu.getFlash()], { type: 'application/octet-stream' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = 'pokeemerald.sav';
  document.body.append(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

async function uploadSave(file) {
  if (!file || !emu) return;
  const bytes = new Uint8Array(await file.arrayBuffer());
  if (bytes.length !== FLASH_SIZE) {
    statusEl.textContent = `expected a ${FLASH_SIZE} byte Emerald .sav file, got ${bytes.length} bytes`;
    return;
  }
  const normalized = normalizeSaveForCurrentBuild(bytes);
  if (!normalized) {
    statusEl.textContent = 'save file does not contain a valid Emerald save slot';
    return;
  }
  storeFlash(normalized);
  statusText = 'restarting with uploaded save...';
  statusEl.textContent = statusText;
  await boot(normalized);
}

function formatSpeed(value) {
  if (value === Infinity) return 'unlimited';
  if (value < 1) return `${value.toFixed(1)}x`;
  if (value < 100) return `${value.toFixed(value < 10 ? 1 : 0)}x`;
  return `${Math.round(value)}x`;
}

function reflectSpeed() {
  const speed = emu.speed;
  speedValue.textContent = formatSpeed(speed);
  speedButtons.forEach((button) => {
    button.classList.toggle('active',
      (button.dataset.speed === '1' && speed === 1) || (button.dataset.speed === 'unlimited' && speed === Infinity));
  });
}

function initialSpeed() {
  if (speedParam === '0') return Infinity;
  const parsed = Number(speedParam);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : 1;
}

function updateFullscreenButton() {
  const isFullscreen = document.fullscreenElement === shell;
  fullscreenButton.textContent = isFullscreen ? 'Exit Big Picture' : 'Big Picture';
  fullscreenButton.classList.toggle('active', isFullscreen);
}

function fpsStatus(displayFps, gameFps) {
  return `${statusText} — Display FPS: ${displayFps}, Game FPS: ${gameFps} (${(gameFps / 60).toFixed(1)}x)`;
}

function reflectButton(name, isPressed) {
  document.querySelectorAll(`[data-key='${name}']`).forEach((el) => el.classList.toggle('pressed', isPressed));
}

function startAudio() {
  if (!emu || emu.audio.ready) return;
  emu.audio.init().then(() => console.log('[audio] ready, sampleRate=', emu.audio.sampleRate)).catch(console.error);
}

window.addEventListener('keydown', (event) => {
  const name = DEFAULT_KEY_MAP.get(event.code);
  if (!name || !emu) return;
  event.preventDefault();
  startAudio();
  emu.setButton(name, true);
  reflectButton(name, true);
});

window.addEventListener('keyup', (event) => {
  const name = DEFAULT_KEY_MAP.get(event.code);
  if (!name || !emu) return;
  event.preventDefault();
  emu.setButton(name, false);
  reflectButton(name, false);
});

const flush = () => emu?.flushSave();
window.addEventListener('beforeunload', flush);
window.addEventListener('pagehide', flush);
window.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') flush(); });

document.querySelectorAll('[data-key]').forEach((button) => {
  const name = button.dataset.key;
  const press = (isPressed) => { if (emu) { emu.setButton(name, isPressed); reflectButton(name, isPressed); } };
  button.addEventListener('pointerdown', (event) => { event.preventDefault(); startAudio(); press(true); });
  button.addEventListener('pointerup', () => press(false));
  button.addEventListener('pointercancel', () => press(false));
  button.addEventListener('pointerleave', () => press(false));
});

speedButtons.forEach((button) => {
  button.addEventListener('click', () => {
    if (!emu) return;
    if (button.dataset.speed === 'unlimited') emu.toggleUnlimitedSpeed();
    else if (button.dataset.speed === '1') emu.setSpeed(1);
    else emu.setSpeed((emu.speed === Infinity ? 1 : emu.speed) * Number(button.dataset.speed));
    reflectSpeed();
  });
});

fullscreenButton.addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await shell.requestFullscreen();
  } catch (error) {
    console.error(error);
    statusEl.textContent = error.stack || String(error);
  }
});
document.addEventListener('fullscreenchange', updateFullscreenButton);
updateFullscreenButton();

downloadSaveButton.addEventListener('click', downloadSave);
uploadSaveInput.addEventListener('change', async () => {
  try {
    await uploadSave(uploadSaveInput.files[0]);
  } catch (error) {
    console.error(error);
    statusEl.textContent = error.stack || String(error);
  } finally {
    uploadSaveInput.value = '';
  }
});

// --- automation (tools/wasm_replay.mjs) ---------------------------------------

function writeS32(ptr, value) {
  emu.views.u16[ptr >> 1] = value & 0xffff;
  emu.views.u16[(ptr + 2) >> 1] = (value >> 16) & 0xffff;
}

function hblankDmaWin0HProbe() {
  const src = 0x0203ff00;
  const dma = REG + REG_OFFSET_DMA0;
  const callDmaStop0 = () => {
    if (typeof emu.instance.exports.WasmDmaStop0 !== 'function') throw new Error('WasmDmaStop0 export unavailable');
    emu.instance.exports.WasmDmaStop0();
  };
  const probeMode = (destMode) => {
    writeS32(dma, src);
    writeS32(dma + 4, REG + REG_OFFSET_WIN0H);
    writeS32(dma + 8, 1 | ((DMA_ENABLE | DMA_START_HBLANK | DMA_REPEAT | destMode) << 16));

    emu.instance.exports.WasmRefreshHblankDmaGpuRegs();
    const activeLine0 = emu.instance.exports.WasmWindowMask(5, 0);
    const activeLine1 = emu.instance.exports.WasmWindowMask(5, 1);
    const activeLine2 = emu.instance.exports.WasmWindowMask(5, 2);
    const activeCached = Boolean(emu.instance.exports.WasmHblankDmaGpuRegActive(REG_OFFSET_WIN0H));

    callDmaStop0();
    const stoppedControl = emu.views.u16[(dma + 10) >> 1];
    emu.instance.exports.WasmRefreshHblankDmaGpuRegs();
    const stoppedLine0 = emu.instance.exports.WasmWindowMask(5, 0);
    const stoppedLine1 = emu.instance.exports.WasmWindowMask(5, 1);
    const stoppedLine2 = emu.instance.exports.WasmWindowMask(5, 2);

    return {
      activeLine0,
      activeLine1,
      activeLine2,
      activeCached,
      stoppedControl,
      stoppedLine0,
      stoppedLine1,
      stoppedLine2,
      stoppedCached: Boolean(emu.instance.exports.WasmHblankDmaGpuRegActive(REG_OFFSET_WIN0H)),
    };
  };
  const saved = {
    dispcnt: emu.views.u16[REG >> 1],
    win0h: emu.views.u16[(REG + REG_OFFSET_WIN0H) >> 1],
    win0v: emu.views.u16[(REG + REG_OFFSET_WIN0V) >> 1],
    winin: emu.views.u16[(REG + REG_OFFSET_WININ) >> 1],
    winout: emu.views.u16[(REG + REG_OFFSET_WINOUT) >> 1],
    src0: emu.views.u16[src >> 1],
    src1: emu.views.u16[(src + 2) >> 1],
    dmaSrc: emu.readU32(dma),
    dmaDest: emu.readU32(dma + 4),
    dmaControl: emu.readU32(dma + 8),
  };
  let result;

  try {
    emu.views.u16[REG >> 1] = 0x2000;
    emu.views.u16[(REG + REG_OFFSET_WIN0H) >> 1] = (20 << 8) | 30;
    emu.views.u16[(REG + REG_OFFSET_WIN0V) >> 1] = HEIGHT;
    emu.views.u16[(REG + REG_OFFSET_WININ) >> 1] = 0x3f;
    emu.views.u16[(REG + REG_OFFSET_WINOUT) >> 1] = 0;
    emu.views.u16[src >> 1] = 10;
    emu.views.u16[(src + 2) >> 1] = (20 << 8) | 30;

    result = {
      fixed: probeMode(DMA_DEST_FIXED),
      reload: probeMode(DMA_DEST_RELOAD),
    };
  } finally {
    emu.views.u16[REG >> 1] = saved.dispcnt;
    emu.views.u16[(REG + REG_OFFSET_WIN0H) >> 1] = saved.win0h;
    emu.views.u16[(REG + REG_OFFSET_WIN0V) >> 1] = saved.win0v;
    emu.views.u16[(REG + REG_OFFSET_WININ) >> 1] = saved.winin;
    emu.views.u16[(REG + REG_OFFSET_WINOUT) >> 1] = saved.winout;
    emu.views.u16[src >> 1] = saved.src0;
    emu.views.u16[(src + 2) >> 1] = saved.src1;
    writeS32(dma, saved.dmaSrc);
    writeS32(dma + 4, saved.dmaDest);
    writeS32(dma + 8, saved.dmaControl);
    emu.instance.exports.WasmRefreshHblankDmaGpuRegs();
  }

  return result;
}

function automationState() {
  const saveBlock1 = emu.readU32(emu.instance.exports.gSaveBlock1Ptr.value);
  const saveBlock2 = emu.readU32(emu.instance.exports.gSaveBlock2Ptr.value);
  const playerAvatar = emu.instance.exports.gPlayerAvatar.value;
  const objectEventId = emu.views.u8[playerAvatar + 5];
  const objectEvent = emu.instance.exports.gObjectEvents.value + objectEventId * 0x24;
  return {
    frame: emu.frame,
    trainerId: emu.views.u16[(saveBlock2 + SAVE_BLOCK2_TRAINER_ID_OFFSET) >> 1],
    x: emu.readS16(saveBlock1),
    y: emu.readS16(saveBlock1 + 2),
    mapGroup: emu.views.u8[saveBlock1 + 4],
    mapNum: emu.views.u8[saveBlock1 + 5],
    elevation: emu.views.u8[objectEvent + 0x0b] & 0x0f,
    objectX: emu.readS16(objectEvent + 0x10),
    objectY: emu.readS16(objectEvent + 0x12),
    objectEventId,
    littlerootTownState: emu.instance.exports.VarGet(0x4050),
    birchLabState: emu.instance.exports.VarGet(0x4084),
    littlerootRivalState: emu.instance.exports.VarGet(0x408d),
    littlerootIntroState: emu.instance.exports.VarGet(0x4092),
    oldaleRivalState: emu.instance.exports.VarGet(0x40c7),
    starterMon: emu.instance.exports.VarGet(0x4023),
  };
}

function automationApi() {
  return {
    ready: automationReady,
    setButton: (name, isPressed) => {
      if (!Object.hasOwn(BUTTONS, name)) throw new Error(`unknown button: ${name}`);
      emu.setButtonHeld(name, isPressed);
      reflectButton(name, isPressed);
    },
    runToFrame: (target) => emu.runToFrame(target),
    screenshot: () => emu.screenshot(),
    hblankDmaWin0HProbe,
    state: automationState,
    frame: () => emu.frame,
  };
}

// --- boot ---------------------------------------------------------------------

async function boot(flash = storedFlash()) {
  if (emu) await emu.destroy();
  const module = await loadGameModule(WASM_URL, { cache: 'no-store' });
  if (!wasmBytes) {
    wasmBytes = await fetch(WASM_URL, { method: 'HEAD', cache: 'no-store' })
      .then((r) => Number(r.headers.get('content-length')) || 0).catch(() => 0);
  }
  emu = await createEmulator({
    canvas, module, flash,
    workletUrl: WORKLET_URL,
    speed: initialSpeed(),
    onFlashChanged: storeFlash,
    onStatus: ({ displayFps, gameFps }) => { statusEl.textContent = fpsStatus(displayFps, gameFps); },
    onError: (error) => { statusEl.textContent = error.stack || String(error); },
  });
  window.pokeemerald = { emu, instance: emu.instance, memory: emu.memory, runFrames: (n, mask) => emu.runFrames(n, mask) };
  statusText = wasmBytes ? `running — ${(wasmBytes / 1024 / 1024).toFixed(1)} MiB wasm` : 'running';
  statusEl.textContent = fpsStatus(0, 0);
  reflectSpeed();
  if (automate) {
    window.pokeemerald.automation = automationApi();
    resolveAutomationReady();
  } else {
    emu.start();
  }
}

boot().catch((error) => {
  console.error(error);
  statusEl.textContent = error.stack || String(error);
});
