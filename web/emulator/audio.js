// Web Audio bridge. The C side (src/m4a_wasm.c) mixes one GBA frame of audio into
// gWasmPcmL/gWasmPcmR (float32, pcmSamplesPerVBlank samples at pcmFreq Hz) every
// WasmRunFrame(). After each frame we resample to the device rate and post the chunk
// to an AudioWorklet ring buffer (audio-worklet.js).
//
// Browsers only allow an AudioContext to start from a user gesture, so `init()` must
// be called from a click/keydown handler.

// SoundInfo layout offsets (include/gba/m4a_internal.h)
const SI_PCM_SAMPLES = 16; // s32 pcmSamplesPerVBlank
const SI_PCM_FREQ = 20; // s32 pcmFreq
const PCM_DMA_BUF_SIZE = 1584;

export function createAudio({ workletUrl, processorName = 'pokeemerald-audio' } = {}) {
  let ctx = null;
  let node = null;
  let gain = null;
  let initPromise = null;
  let muted = false;
  let volume = 1;
  let enabled = true;
  let underruns = 0;
  let fed = 0;

  async function init() {
    if (initPromise) return initPromise;
    initPromise = (async () => {
      if (!workletUrl) throw new Error('audio: workletUrl not provided');
      ctx = new AudioContext();
      if (ctx.state === 'suspended') await ctx.resume();
      await ctx.audioWorklet.addModule(workletUrl);
      node = new AudioWorkletNode(ctx, processorName, { outputChannelCount: [2] });
      node.port.onmessage = ({ data }) => {
        if (data?.type === 'underrun') underruns += data.count ?? 1;
      };
      gain = ctx.createGain();
      gain.gain.value = muted ? 0 : volume;
      node.connect(gain).connect(ctx.destination);
    })();
    try {
      await initPromise;
    } catch (error) {
      initPromise = null;
      throw error;
    }
    return initPromise;
  }

  function applyGain() {
    if (gain) gain.gain.value = muted ? 0 : volume;
  }

  /**
   * Push one frame of mixed PCM. `instance`/`memory` are the live wasm objects;
   * `readS32` reads a little-endian int32 at a byte address. `rate` is the emulation
   * speed relative to realtime: the frame is time-compressed by that factor so that
   * fast-forward plays sped-up (higher-pitched) audio instead of overflowing the ring.
   */
  function feed(instance, memory, readS32, rate = 1) {
    if (!node || !enabled || muted) return;
    const siExport = instance.exports.gSoundInfo;
    const lExport = instance.exports.gWasmPcmL;
    const rExport = instance.exports.gWasmPcmR;
    if (!siExport || !lExport || !rExport) return; // built without audio
    const base = siExport.value;
    const n = readS32(base + SI_PCM_SAMPLES);
    const srcRate = readS32(base + SI_PCM_FREQ);
    if (n <= 0 || n > PCM_DMA_BUF_SIZE || srcRate <= 0) return;

    const dstRate = ctx.sampleRate;
    const rawL = new Float32Array(memory.buffer, lExport.value, n);
    const rawR = new Float32Array(memory.buffer, rExport.value, n);

    // Linear-interpolation resample srcRate -> dstRate, compressed by the speed factor.
    const speed = rate > 0 && Number.isFinite(rate) ? rate : 1;
    const outLen = Math.max(1, Math.round(n * dstRate / (srcRate * speed)));
    const L = new Float32Array(outLen);
    const R = new Float32Array(outLen);
    const step = (n - 1) / Math.max(outLen - 1, 1);
    for (let i = 0; i < outLen; i++) {
      const si = i * step;
      const lo = Math.floor(si);
      const hi = Math.min(lo + 1, n - 1);
      const t = si - lo;
      L[i] = rawL[lo] + t * (rawL[hi] - rawL[lo]);
      R[i] = rawR[lo] + t * (rawR[hi] - rawR[lo]);
    }
    node.port.postMessage({ L, R }, [L.buffer, R.buffer]);
    fed++;
  }

  return {
    init,
    feed,
    get ready() { return Boolean(node); },
    get context() { return ctx; },
    get sampleRate() { return ctx?.sampleRate ?? 0; },
    get muted() { return muted; },
    setMuted(value) { muted = Boolean(value); applyGain(); },
    get volume() { return volume; },
    setVolume(value) { volume = Math.min(1, Math.max(0, Number(value) || 0)); applyGain(); },
    /** Disable feeding entirely (audio keeps running, output is silence). */
    setEnabled(value) { enabled = Boolean(value); },
    async suspend() { if (ctx && ctx.state === 'running') await ctx.suspend(); },
    async resume() { if (ctx && ctx.state === 'suspended') await ctx.resume(); },
    stats() { return { underruns, fed }; },
    async close() {
      if (ctx) await ctx.close();
      ctx = node = gain = null;
      initPromise = null;
    },
  };
}
