// GBA BIOS calls and other functions the wasm module imports from the host.
// Lifted from the upstream web/app.js. Each emulator instance gets its own closure
// over its memory views, so several instances can coexist.

/**
 * @param {{ u8: Uint8Array, u16: Uint16Array, u32: Uint32Array }} views - live views; call
 *   `refresh()` on them if memory.buffer ever changes (it cannot: memory is fixed size).
 */
export function createBios(views) {
  // Views are read at call time: memory does not exist yet when the import object is built.

  function copy(src, dst, count, size, fill) {
    for (let i = 0; i < count; i++) {
      const from = fill ? src : src + i * size;
      views.u8.set(views.u8.subarray(from, from + size), dst + i * size);
    }
  }

  function lz77(src, dst) {
    const size = views.u8[src + 1] | (views.u8[src + 2] << 8) | (views.u8[src + 3] << 16);
    let s = src + 4;
    let d = dst;
    const end = dst + size;
    while (d < end) {
      const flags = views.u8[s++];
      for (let bit = 7; bit >= 0 && d < end; bit--) {
        if (flags & (1 << bit)) {
          const pair = (views.u8[s] << 8) | views.u8[s + 1];
          s += 2;
          let length = (pair >> 12) + 3;
          const disp = (pair & 0xfff) + 1;
          while (length-- && d < end) {
            views.u8[d] = views.u8[d - disp];
            d++;
          }
        } else {
          views.u8[d++] = views.u8[s++];
        }
      }
    }
  }

  function rl(src, dst) {
    const size = views.u8[src + 1] | (views.u8[src + 2] << 8) | (views.u8[src + 3] << 16);
    let s = src + 4;
    let d = dst;
    const end = dst + size;
    while (d < end) {
      const flag = views.u8[s++];
      if (flag & 0x80) {
        let count = (flag & 0x7f) + 3;
        const value = views.u8[s++];
        while (count-- && d < end) views.u8[d++] = value;
      } else {
        let count = (flag & 0x7f) + 1;
        while (count-- && d < end) views.u8[d++] = views.u8[s++];
      }
    }
  }

  function readCString(ptr) {
    let out = '';
    while (views.u8[ptr]) out += String.fromCharCode(views.u8[ptr++]);
    return out;
  }

  function readS16(ptr) {
    return views.u16[ptr >> 1] << 16 >> 16;
  }

  function readS32(ptr) {
    return (views.u16[ptr >> 1] | (views.u16[(ptr + 2) >> 1] << 16)) | 0;
  }

  function writeS16(ptr, value) {
    views.u16[ptr >> 1] = value & 0xffff;
  }

  function writeS32(ptr, value) {
    views.u16[ptr >> 1] = value & 0xffff;
    views.u16[(ptr + 2) >> 1] = (value >> 16) & 0xffff;
  }

  function affineTerms(xScale, yScale, rotation) {
    const angle = rotation * Math.PI * 2 / 0x10000;
    const sin = Math.sin(angle) * 256;
    const cos = Math.cos(angle) * 256;
    return {
      pa: cos * xScale / 256,
      pb: -sin * xScale / 256,
      pc: sin * yScale / 256,
      pd: cos * yScale / 256,
    };
  }

  function bgAffineSet(src, dest, count) {
    for (let i = 0; i < count; i++) {
      const s = src + i * 20;
      const d = dest + i * 16;
      const texX = readS32(s);
      const texY = readS32(s + 4);
      const scrX = readS16(s + 8);
      const scrY = readS16(s + 10);
      const { pa, pb, pc, pd } = affineTerms(readS16(s + 12), readS16(s + 14), views.u16[(s + 16) >> 1]);
      const a = pa | 0;
      const b = pb | 0;
      const c = pc | 0;
      const e = pd | 0;
      writeS16(d, a);
      writeS16(d + 2, b);
      writeS16(d + 4, c);
      writeS16(d + 6, e);
      writeS32(d + 8, (texX - scrX * a - scrY * b) | 0);
      writeS32(d + 12, (texY - scrX * c - scrY * e) | 0);
    }
  }

  function objAffineSet(src, dest, count, offset) {
    for (let i = 0; i < count; i++) {
      const s = src + i * 6;
      const d = dest + i * offset * 4;
      const { pa, pb, pc, pd } = affineTerms(readS16(s), readS16(s + 2), views.u16[(s + 4) >> 1]);
      writeS16(d, pa | 0);
      writeS16(d + offset, pb | 0);
      writeS16(d + offset * 2, pc | 0);
      writeS16(d + offset * 3, pd | 0);
    }
  }

  function copyOamMatrices(src, dest, dummy, oamCount, oamLimit, oamEnd) {
    const dummyWord0 = views.u32[dummy >> 2];
    const dummyWord1 = views.u32[(dummy + 4) >> 2];
    for (let entry = oamCount; entry < oamLimit; entry++) {
      const output = (dest + entry * 8) >> 2;
      views.u32[output] = dummyWord0;
      views.u32[output + 1] = dummyWord1;
    }
    const wholeMatrices = oamEnd >> 2;
    for (let matrix = 0; matrix < wholeMatrices; matrix++) {
      const source = (src + matrix * 8) >> 1;
      const output = (dest + matrix * 32 + 6) >> 1;
      views.u16[output] = views.u16[source];
      views.u16[output + 4] = views.u16[source + 1];
      views.u16[output + 8] = views.u16[source + 2];
      views.u16[output + 12] = views.u16[source + 3];
    }
    for (let entry = wholeMatrices * 4; entry < oamEnd; entry++) {
      views.u16[(dest + entry * 8 + 6) >> 1] = views.u16[(src + entry * 2) >> 1];
    }
  }

  function importsFor(module) {
    const env = {};
    for (const item of WebAssembly.Module.imports(module)) {
      if (item.kind !== 'function') continue;
      env[item.name] = (...args) => {
        switch (item.name) {
          case 'CpuSet': return copy(args[0], args[1], args[2] & 0x1fffff, (args[2] >>> 26) & 1 ? 4 : 2, (args[2] >>> 24) & 1);
          case 'CpuFastSet': return copy(args[0], args[1], args[2] & 0x1fffff, 4, (args[2] >>> 24) & 1);
          case 'LZ77UnCompWram':
          case 'LZ77UnCompVram': return lz77(args[0], args[1]);
          case 'RLUnCompWram':
          case 'RLUnCompVram': return rl(args[0], args[1]);
          case 'BgAffineSet': return bgAffineSet(args[0], args[1], args[2]);
          case 'ObjAffineSet': return objAffineSet(args[0], args[1], args[2], args[3]);
          case 'WasmCopyOamMatrices': return copyOamMatrices(args[0], args[1], args[2], args[3], args[4], args[5]);
          case 'Div': return args[1] ? (args[0] / args[1]) | 0 : 0;
          case 'Sqrt': return Math.sqrt(args[0]) | 0;
          case 'strcmp': return readCString(args[0]).localeCompare(readCString(args[1]));
          default: return 0;
        }
      };
    }
    return { env };
  }

  return { importsFor, readS16, readS32, writeS16, writeS32, readCString };
}
