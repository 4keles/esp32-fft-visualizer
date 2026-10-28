"use strict";
// engine.js - WebAssembly wrapper around the FFT-VIZ core (fftviz.wasm), shared by the Web Worker and the
// main-thread fallback. It feeds 48 kHz ADC-domain samples to the analyzer and the per-mode LED engines and
// returns, per request, everything the viewer draws. No DSP or LED logic lives here: it all runs in the core.
(function (root) {
  const FS = 48000;             // ADC rate of the core
  const FRAME_HZ = 60;          // analysis frames per second (one frame = one 800-sample ADC block)
  const BLOCK = 800;
  const PRE_FRAMES = 240;       // 4 s of noise-only lead-in before the content, like a board that has just powered up
  const CAL_FRAME = 90;         // `cal` is issued 1.5 s into the lead-in and learns for 2 s
  const CAL_LEARN = 120;
  const WARM_FRAMES = 240;      // analyzer warm-up before a seek target
  const LED_WARM_FRAMES = 180;  // LED engines run (and settle) for the last 3 s before a seek target
  const JUMP_FRAMES = 120;      // a jump further ahead than this is a seek, not playback
  const LEVEL_DB = -32;         // fixed level rule: the file enters the 12-bit ADC domain at -32 dBFS (no normalization)
  const NOISE_AMP = 14.7;       // triangular board noise of +-14.7 LSB: rms 6 LSB
  const MODE_MASK_ALL = 0x1ff;
  const HF = 10;                // history row: bass, mid, treble, loud, beatEnv, beat, onset, silent, bpm, ceiling
  const S_SC = 240;             // scalar block offset in viz_snap(), see sim/wasm_api.cpp
  const SC = { FRAME: 0, BEAT: 1, ONSET: 2, SILENT: 3, HUE: 4, BPM: 5, BEATENV: 6, BEATSTR: 7, BASS: 8, MID: 9, TREBLE: 10,
    LOUD: 11, CEILING: 12, BFP: 13, MA: 14, LIMITER: 15, MODE: 16, CLIP: 17, DC: 18, DEMAND: 19, BUDGET: 20 };

  // Demo program built from the core's own synthetic scenarios (no recorded audio): [scenario, gain dB, seconds].
  const DEMO = [[0, 0, 3], [3, -26, 12], [4, -26, 30], [2, -26, 8], [4, -20, 22], [5, -26, 8], [4, -32, 7], [0, 0, 3]];

  function xorshift(s) { s ^= s << 13; s ^= s >>> 17; s ^= s << 5; return s >>> 0; }

  // Converts decoded audio (Float32 channels at 48 kHz) to the ADC domain the board sees: mono mix, fixed
  // -32 dB level, mid-scale 2048 bias, synthetic board noise (rms 6 LSB). The first PRE_FRAMES frames are
  // noise only. Yields to the event loop every ~1M samples so the page stays responsive.
  async function toAdc(channels, progress) {
    const pre = PRE_FRAMES * BLOCK, len = channels[0].length, nch = channels.length;
    const out = new Uint16Array(pre + len);
    const k = 2047 * Math.pow(10, LEVEL_DB / 20), inv = 1 / nch, na = NOISE_AMP / 65536;
    let s = 0x2545f491;
    const noise = () => { s = xorshift(s); return ((s & 0xffff) + (s >>> 16) - 65535) * na; };
    for (let i = 0; i < pre; i++) { const v = (2048.5 + noise()) | 0; out[i] = v < 0 ? 0 : v > 4095 ? 4095 : v; }
    const SLICE = 1 << 20;
    for (let a = 0; a < len; a += SLICE) {
      const b = Math.min(len, a + SLICE);
      for (let i = a; i < b; i++) {
        let x = 0;
        for (let c = 0; c < nch; c++) x += channels[c][i];
        const v = (2048.5 + x * inv * k + noise()) | 0;
        out[pre + i] = v < 0 ? 0 : v > 4095 ? 4095 : v;
      }
      if (progress) progress(b / len);
      if (b < len) await new Promise((r) => setTimeout(r, 0));
    }
    return out;
  }

  function wavFromAdc(adc, from) {  // 16-bit mono 48 kHz WAV of the ADC offsets from `from`, scaled to a comfortable peak
    const n = adc.length - from;
    let peak = 1;
    for (let i = from; i < adc.length; i++) { const d = Math.abs(adc[i] - 2048); if (d > peak) peak = d; }
    const g = 0.7 * 32767 / peak, buf = new ArrayBuffer(44 + n * 2), dv = new DataView(buf);
    const tag = (o, t) => { for (let i = 0; i < 4; i++) dv.setUint8(o + i, t.charCodeAt(i)); };
    tag(0, "RIFF"); dv.setUint32(4, 36 + n * 2, true); tag(8, "WAVE"); tag(12, "fmt "); dv.setUint32(16, 16, true);
    dv.setUint16(20, 1, true); dv.setUint16(22, 1, true); dv.setUint32(24, FS, true); dv.setUint32(28, FS * 2, true);
    dv.setUint16(32, 2, true); dv.setUint16(34, 16, true); tag(36, "data"); dv.setUint32(40, n * 2, true);
    for (let i = 0; i < n; i++) dv.setInt16(44 + i * 2, Math.round((adc[from + i] - 2048) * g), true);
    return buf;
  }

  const lut565 = new Uint32Array(65536);  // RGB565 (byte-swapped as the core writes it) -> little-endian RGBA
  for (let v = 0; v < 65536; v++) {
    const c = ((v >> 8) | (v << 8)) & 0xffff, r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
    lut565[v] = (255 << 24) | (((b5 << 3) | (b5 >> 2)) << 16) | (((g6 << 2) | (g6 >> 4)) << 8) | ((r5 << 3) | (r5 >> 2));
  }

  class FvEngine {
    static async create(bytes) {
      const { instance } = await WebAssembly.instantiate(bytes, {});
      return new FvEngine(instance.exports);
    }

    constructor(w) {
      this.w = w;
      w.viz_init(1);
      this.nLeds = w.viz_led_count();
      this.ids = {};
      for (let i = 0, n = w.viz_param_count(); i < n; i++) this.ids[this.str(w.viz_param_name(i))] = i;
      this.modeNames = Array.from({ length: w.viz_mode_count() }, (_, i) => this.str(w.viz_mode_name(i)));
      this.themeNames = Array.from({ length: w.viz_theme_count() }, (_, i) => this.str(w.viz_theme_name(i)));
      this.statN = w.viz_mode_stat_n();
      this.adc = null;
      this.frames = 0;
      this.cur = 0;
      this.sel = 0;
      this.allModes = true;
      this.mask = MODE_MASK_ALL;
      this.screenOn = false;
      this.dirty = true;
      this.tick = 0;
      this.buf = null;
      this.screen32 = new Uint32Array(240 * 240);
    }

    str(p) {
      const m = new Uint8Array(this.w.memory.buffer);
      let e = p;
      while (m[e]) e++;
      return new TextDecoder().decode(m.subarray(p, e));
    }

    views() {
      const w = this.w;
      if (this.buf === w.memory.buffer) return;
      this.buf = w.memory.buffer;
      this.inV = new Uint16Array(this.buf, w.viz_in(), w.viz_in_len());
      this.snapV = new Int32Array(this.buf, w.viz_snap(), w.viz_snap_len());
      this.statV = new Int32Array(this.buf, w.viz_mode_stats(), 9 * this.statN);
      this.fbV = new Uint16Array(this.buf, w.viz_fb(), 240 * 240);
    }

    info() {
      const w = this.w, d = (n) => w.viz_param_def(this.ids[n]);
      return { modes: this.modeNames, themes: this.themeNames, nLeds: this.nLeds, lim: { bri: d("bri"), agcmin: d("agcmin") } };
    }

    setParam(name, value) {
      this.w.viz_set(this.ids[name], value);
    }

    configure(c) {
      if (c.sel !== undefined && c.sel !== this.sel) { this.sel = c.sel; this.w.viz_set(this.ids.mode, c.sel); }
      if (c.allModes !== undefined) this.allModes = c.allModes;
      if (c.screen !== undefined) this.screenOn = c.screen;
      const mask = this.allModes ? MODE_MASK_ALL : 1 << this.sel;
      if (mask !== this.mask) { this.mask = mask; this.dirty = true; }  // newly enabled engines start from a replay
    }

    setSource(adc) {
      this.adc = adc;
      this.frames = Math.max(0, Math.floor((adc.length - PRE_FRAMES * BLOCK) / BLOCK));
      this.cur = 0;
      this.dirty = true;
    }

    // The built-in demo: the core's synthetic scenarios rendered straight into the ADC domain, with the same lead-in.
    buildDemo() {
      const w = this.w, pre = PRE_FRAMES * BLOCK;
      let total = pre;
      for (const s of DEMO) total += s[2] * FS;
      const adc = new Uint16Array(total);
      this.views();
      let pos = 0;
      const render = (sc, gain, n, seed) => {
        w.viz_reset(seed);
        w.viz_synth(sc, gain);
        for (let left = n; left > 0;) {
          const c = Math.min(left, this.inV.length);
          w.viz_synth_render(c);
          adc.set(this.inV.subarray(0, c), pos);
          pos += c;
          left -= c;
        }
      };
      render(0, 0, pre, 1);
      DEMO.forEach((s, i) => render(s[0], s[1], s[2] * FS, i + 2));
      w.viz_reset(1);
      return { adc, wav: wavFromAdc(adc, pre) };
    }

    feed(from, to) {  // ADC samples [from, to) in one-frame blocks; returns frames produced
      const w = this.w;
      let n = 0;
      for (let p = from; p < to; p += BLOCK) {
        this.inV.set(this.adc.subarray(p, p + BLOCK));
        n += w.viz_push(BLOCK);
      }
      return n;
    }

    // Rebuilds the engine state for content frame `f`: lead-in with calibration, then a warm-up of the
    // audio before `f`. Deterministic, so a seek or a parameter change replays the same way.
    prime(f) {
      const w = this.w, pre = PRE_FRAMES * BLOCK, mask = this.mask;
      this.views();
      w.viz_reset(1);
      w.viz_modes(0);
      for (let b = 0; b < PRE_FRAMES; b++) {
        this.feed(b * BLOCK, (b + 1) * BLOCK);
        if (b === CAL_FRAME) w.viz_calibrate(CAL_LEARN);
      }
      const warm = Math.max(0, f - WARM_FRAMES), ledFrom = Math.max(0, f - LED_WARM_FRAMES);
      if (ledFrom === warm) w.viz_modes(mask);
      for (let i = warm; i < f; i++) {
        if (i === ledFrom) w.viz_modes(mask);
        this.feed(pre + i * BLOCK, pre + (i + 1) * BLOCK);
      }
      w.viz_modes(mask);
      this.cur = f;
      this.dirty = false;
    }

    // Advances to content frame `to` (priming when it is a seek) and returns the data to draw.
    run(to, force) {
      if (!this.adc) return null;
      const w = this.w, n = this.nLeds, pre = PRE_FRAMES * BLOCK;
      to = Math.max(0, Math.min(this.frames, to));
      const t0 = performance.now();
      this.views();
      let seeked = false;
      if (force || this.dirty || to < this.cur || to - this.cur > JUMP_FRAMES) { this.prime(to); seeked = true; }
      const count = to - this.cur, hist = new Int32Array(count * HF), histLed = new Uint8Array(count * n * 3);
      for (let i = 0; i < count; i++) {
        this.feed(pre + this.cur * BLOCK, pre + (this.cur + 1) * BLOCK);
        this.cur++;
        w.viz_snapshot();
        const s = this.snapV, o = S_SC, h = i * HF;
        hist[h] = s[o + SC.BASS]; hist[h + 1] = s[o + SC.MID]; hist[h + 2] = s[o + SC.TREBLE]; hist[h + 3] = s[o + SC.LOUD];
        hist[h + 4] = s[o + SC.BEATENV]; hist[h + 5] = s[o + SC.BEAT]; hist[h + 6] = s[o + SC.ONSET]; hist[h + 7] = s[o + SC.SILENT];
        hist[h + 8] = s[o + SC.BPM]; hist[h + 9] = s[o + SC.CEILING];
        histLed.set(new Uint8Array(this.buf, w.viz_mode_leds(this.sel), n * 3), i * n * 3);
      }
      if (this.screenOn) w.viz_render(this.tick++); else w.viz_snapshot();
      const leds = new Uint8Array(9 * n * 3);
      for (let m = 0; m < 9; m++) if (this.allModes || m === this.sel) leds.set(new Uint8Array(this.buf, w.viz_mode_leds(m), n * 3), m * n * 3);
      const reply = { frame: this.cur, seeked, hist, histLed, leds, stats: this.statV.slice(), snap: this.snapV.slice(), screen: null, ms: 0, count };
      if (this.screenOn) {
        const fb = this.fbV, d = this.screen32;
        for (let i = 0; i < d.length; i++) d[i] = lut565[fb[i]];
        reply.screen = d.slice();
      }
      reply.ms = performance.now() - t0;
      return reply;
    }
  }

  // Message host: the Web Worker and the main-thread fallback both run this and differ only in how replies travel.
  class Host {
    constructor(reply) {
      this.reply = reply;
      this.eng = null;
    }

    async handle(m) {
      try {
        const e = this.eng;
        switch (m.type) {
          case "init": {
            const res = await fetch(m.wasmUrl);
            if (!res.ok) throw new Error("fftviz.wasm: HTTP " + res.status);
            this.eng = await FvEngine.create(await res.arrayBuffer());
            this.reply({ type: "ready", info: this.eng.info() });
            break;
          }
          case "source":
            e.setSource(m.adc);
            this.reply({ type: "source", frames: e.frames });
            break;
          case "demo": {
            const d = e.buildDemo();
            e.setSource(d.adc);
            this.reply({ type: "demo", frames: e.frames, wav: d.wav }, [d.wav]);
            break;
          }
          case "param": e.setParam(m.name, m.value); break;
          case "config": e.configure(m); break;
          case "run": {
            const r = e.run(m.to, m.force);
            if (r) {
              r.type = "frame";
              const tr = [r.hist.buffer, r.histLed.buffer, r.leds.buffer, r.stats.buffer, r.snap.buffer];
              if (r.screen) tr.push(r.screen.buffer);
              this.reply(r, tr);
            } else this.reply({ type: "frame", empty: true });
            break;
          }
        }
      } catch (err) {
        this.reply({ type: "error", message: String((err && err.message) || err) });
      }
    }
  }

  const api = { FvEngine, Host, toAdc, wavFromAdc, FS, FRAME_HZ, BLOCK, PRE_FRAMES, HF, SC, S_SC, LEVEL_DB, DEMO };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.FvCore = api;
})(typeof self !== "undefined" ? self : globalThis);
