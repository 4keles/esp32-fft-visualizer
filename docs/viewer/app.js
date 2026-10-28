"use strict";
// FFT-VIZ live viewer: UI, audio clock and drawing. The analyzer and the nine LED engines run in the WebAssembly core
// (engine.js, in a Web Worker); this file only decodes audio, drives the core from the audio clock and draws what it returns.
(() => {
  const Core = window.FvCore;
  const $ = (id) => document.getElementById(id);
  const FPS = Core.FRAME_HZ, HF = Core.HF, SC = Core.SC, S_SC = Core.S_SC;
  const DBU = 85.0403;              // core level units per dB
  const HISTN = 600;                // history length in frames (10 s)
  const DEMO_START = 25;            // the demo opens paused at 25 s, where all modes are busy

  const MODE_TEXT = {
    AURORA: "Frequency → position: low frequencies at the left end, high frequencies at the right end; color flows with position. Bands below the threshold glow as very dim embers. When the analyzer reports silence, a slowly breathing idle rainbow is blended in.",
    SPARKLE: "The same band energy as AURORA, mapped through a fixed shuffled order, so the energy appears scattered across the strip. The scatter is intended.",
    MIRROR: "Center to ends: bass in the middle, treble at both ends, symmetric. A glow at the center on a beat. Color follows the spectral centroid.",
    SPECTRUM: "Eight segments of 15 LEDs; each fills up to the highest of its five bands, odd segments in the opposite direction. The white cap is the peak marker. An empty segment is black, which is normal.",
    BASS: "A soft pulse in the center: its radius follows the bass envelope, with white added on a beat. Without bass only a very dim base remains.",
    WATERFALL: "History sliding from the center to both ends: the new color enters in the middle (brightness follows the overall level) and fades by ×0.97 at every step.",
    FIRE: "Two flames rise from both ends of the strip toward the center; the center is deliberately the darkest place. Sparks increase with bass and beats.",
    COMET: "On a pale AURORA base, comets shoot out from the center on a beat (alternating left and right); a white spark on a treble beat. Tails fade out when they leave the strip.",
    DEMO: "Cycles through modes 0…7 in order: each sub-mode lasts 32 beats or at most 30 s, whichever comes first, with a 30-frame crossfade.",
  };

  const GAMMA = Uint8Array.from({ length: 256 }, (_, v) => Math.round(255 * Math.pow(v / 255, 1 / 2.2)));
  const IDENT = Uint8Array.from({ length: 256 }, (_, v) => v);

  const S = {
    ready: false, loaded: false, busy: false, frames: 0, dur: 0, n: 120, modes: [], themes: [],
    sel: 0, view: "glow", allModes: true, screenOn: true, sens: -45, bri: 255, theme: 0, pin: null,
    inflight: false, shown: -1, needFrame: true, force: false, last: null, head: 0,
    ms: 0, drawMs: 0, dragging: false, objectUrl: null, clockAt: 0, perfAt: 0, backend: "worker",
  };

  const el = (tag, cls) => { const e = document.createElement(tag); if (cls) e.className = cls; return e; };
  const fmt = (t) => { const m = Math.floor(t / 60); return m + ":" + (t - m * 60).toFixed(1).padStart(4, "0"); };
  const setText = (node, v) => { if (node.textContent !== v) node.textContent = v; };
  const fit = (c) => {
    const d = Math.min(window.devicePixelRatio || 1, 2), w = c.clientWidth, h = c.clientHeight;
    if (c.width !== Math.round(w * d) || c.height !== Math.round(h * d)) { c.width = Math.round(w * d); c.height = Math.round(h * d); }
    const x = c.getContext("2d");
    x.setTransform(d, 0, 0, d, 0, 0);
    return [x, w, h];
  };

  // ------------------------------------------------------------------ messages
  const info = (t) => setText($("srcInfo"), t);
  function fatal(msg) {
    const f = $("fatal");
    f.textContent = location.protocol === "file:"
      ? "Browsers block loading the WebAssembly core from a file:// address. Serve the docs folder over http, for example: python3 -m http.server --directory docs"
      : msg;
    f.hidden = false;
    for (const id of ["pick", "demoBtn", "play", "seek", "speed", "vol", "loop"]) $(id).disabled = true;
    info("The core could not start.");
  }

  // ------------------------------------------------------------------ backend: Web Worker, or the same code on the main thread
  let worker = null, host = null;
  const wasmUrl = new URL("viewer/fftviz.wasm", document.baseURI).href;
  let send = () => {};
  function useWorker() {
    worker = new Worker("viewer/worker.js");
    worker.onmessage = (e) => onReply(e.data);
    worker.onerror = (ev) => {
      ev.preventDefault();
      if (S.ready) { fatal("The analysis worker stopped unexpectedly. Reload the page."); return; }
      worker = null;
      useInline();
    };
    send = (m, tr) => worker.postMessage(m, tr || []);
    S.backend = "worker";
  }
  function useInline() {
    host = new Core.Host(onReply);
    send = (m) => setTimeout(() => host.handle(m), 0);
    S.backend = "main thread";
    send({ type: "init", wasmUrl });
  }
  function startBackend() {
    if (typeof WebAssembly !== "object") {
      fatal("This browser does not support WebAssembly, which the FFT-VIZ core needs. The analyzer and the nine LED modes have no JavaScript fallback; use a current Chrome, Edge, Firefox or Safari.");
      return;
    }
    try { useWorker(); send({ type: "init", wasmUrl }); } catch (e) { useInline(); }
  }

  function onReply(m) {
    switch (m.type) {
      case "ready": onReady(m.info); break;
      case "demo": {
        S.frames = m.frames;
        const url = URL.createObjectURL(new Blob([m.wav], { type: "audio/wav" }));
        setSource(url, DEMO_START, "Built-in demo: synthetic scenarios from the core (silence, kick, music-like mix, sweep, noise)");
        break;
      }
      case "source": S.frames = m.frames; break;
      case "frame": if (!m.empty) onFrame(m); else S.inflight = false; break;
      case "error": S.inflight = false; S.busy = false; fatal("The core reported an error: " + m.message); break;
    }
  }

  // ------------------------------------------------------------------ audio element = the clock
  const audio = new Audio();
  audio.preload = "auto";
  audio.volume = 0.8;
  const PLAY = "M7 4.5v15l12-7.5z", PAUSE = "M6 4.5h4.5v15H6zM13.5 4.5H18v15h-4.5z";
  const syncPlay = () => {
    $("playIcon").setAttribute("d", audio.paused ? PLAY : PAUSE);
    $("play").setAttribute("aria-label", audio.paused ? "Play" : "Pause");
  };
  audio.addEventListener("play", syncPlay);
  audio.addEventListener("pause", syncPlay);
  audio.addEventListener("ended", () => { syncPlay(); S.needFrame = true; });
  audio.addEventListener("error", () => {
    if (S.loaded) info("This browser analyzed the file but cannot play it. Try another format.");
  });

  function setSource(url, startAt, label) {
    audio.pause();
    if (S.objectUrl) URL.revokeObjectURL(S.objectUrl);
    S.objectUrl = url;
    S.dur = S.frames / FPS;
    audio.src = url;
    audio.loop = $("loop").checked;
    audio.currentTime = startAt;
    $("seek").max = String(S.dur);
    $("seek").value = String(startAt);
    setText($("tEnd"), fmt(S.dur));
    S.loaded = true;
    S.busy = false;
    S.shown = -1;
    S.force = true;
    S.needFrame = true;
    for (const id of ["play", "seek", "pick", "demoBtn"]) $(id).disabled = false;
    info(label);
  }

  function togglePlay() {
    if (!S.loaded) return;
    if (audio.paused) {
      if (audio.ended || audio.currentTime >= S.dur - 0.05) audio.currentTime = 0;
      audio.play().catch((e) => info("Playback was blocked: " + e.message));
    } else audio.pause();
  }
  const seekTo = (t) => { if (S.loaded) { audio.currentTime = Math.max(0, Math.min(S.dur, t)); S.needFrame = true; } };

  // ------------------------------------------------------------------ loading a file
  async function loadFile(file) {
    if (!file || S.busy || !S.ready) return;
    if (!(window.OfflineAudioContext || window.webkitOfflineAudioContext)) { info("This browser cannot decode audio files. The built-in demo still works."); return; }
    S.busy = true;
    audio.pause();
    for (const id of ["pick", "demoBtn", "play"]) $(id).disabled = true;
    try {
      info("Reading " + file.name + " …");
      const buf = await file.arrayBuffer();
      info("Decoding " + file.name + " …");
      const Ctx = window.OfflineAudioContext || window.webkitOfflineAudioContext;
      const dec = await new Ctx(1, 1, Core.FS).decodeAudioData(buf);
      const chans = Array.from({ length: dec.numberOfChannels }, (_, c) => dec.getChannelData(c));
      const adc = await Core.toAdc(chans, (p) => info("Preparing " + file.name + " … " + Math.round(p * 100) + " %"));
      S.frames = Math.max(1, Math.floor((adc.length - Core.PRE_FRAMES * Core.BLOCK) / Core.BLOCK));
      send({ type: "source", adc }, [adc.buffer]);
      setSource(URL.createObjectURL(file), 0, file.name + " · " + fmt(S.frames / FPS) + " · decoded and analyzed in this browser, nothing uploaded");
    } catch (err) {
      console.warn("Could not load the file:", err);
      S.busy = false;
      for (const id of ["pick", "demoBtn"]) $(id).disabled = false;
      $("play").disabled = !S.loaded;
      info("Could not decode " + file.name + ". Try WAV, MP3, AAC, FLAC or OGG.");
    }
  }

  // ------------------------------------------------------------------ LED strips
  class Strip {
    constructor(host, big) {
      this.host = host; this.big = big; this.w = 0; this.h = 0; this.d = 1; this.redraw = null;
      this.core = el("canvas", "core"); this.bloom = el("canvas", "bloom");
      host.append(this.core, this.bloom);
      if (big) { this.halo = el("canvas", "halo"); host.append(this.halo); }
      this.x = this.core.getContext("2d"); this.bx = this.bloom.getContext("2d"); this.hx = big ? this.halo.getContext("2d") : null;
      new ResizeObserver(() => this.resize()).observe(host);
    }
    resize() {
      const w = this.host.clientWidth, h = this.host.clientHeight;
      if (!w || !h) return;
      const d = Math.min(window.devicePixelRatio || 1, 2);
      for (const c of [this.core, this.bloom, this.halo]) if (c) { c.width = Math.round(w * d); c.height = Math.round(h * d); }
      this.w = w; this.h = h; this.d = d;
      if (this.redraw) this.redraw();
    }
    pitch() { return (this.w - 2 * (this.big ? 16 : 10)) / S.n; }
    ledAt(clientX) {
      const r = this.host.getBoundingClientRect(), pad = this.big ? 16 : 10;
      return Math.max(0, Math.min(S.n - 1, Math.floor((clientX - r.left - pad) / this.pitch())));
    }
    draw(px, off, map, pin) {
      const { x, w, h, d, big } = this;
      if (!w) return;
      const n = S.n, pad = big ? 16 : 10, pitch = (w - 2 * pad) / n, cy = h / 2;
      const pcbH = big ? 36 : 13, bw = Math.min(pitch * 0.7, big ? 12 : 6.5), bh = big ? 19 : 8.5;
      x.setTransform(d, 0, 0, d, 0, 0);
      x.clearRect(0, 0, w, h);
      // flexible strip: dark base, copper edge lines and solder pads between LED groups
      x.fillStyle = "#0b0d15";
      x.fillRect(pad - 6, cy - pcbH / 2, w - 2 * pad + 12, pcbH);
      x.fillStyle = "rgba(214,160,92,.20)";
      x.fillRect(pad - 6, cy - pcbH / 2, w - 2 * pad + 12, 1);
      x.fillRect(pad - 6, cy + pcbH / 2 - 1, w - 2 * pad + 12, 1);
      if (big) {
        x.fillStyle = "rgba(214,160,92,.30)";
        for (let i = 3; i < n; i += 3) { const px0 = pad + i * pitch - 1.5; x.fillRect(px0, cy - pcbH / 2 + 3, 3, 3); x.fillRect(px0, cy + pcbH / 2 - 6, 3, 3); }
      }
      for (let i = 0; i < n; i++) {
        const o = off + i * 3, r = px[o], g = px[o + 1], b = px[o + 2], m = r > g ? (r > b ? r : b) : (g > b ? g : b);
        const cx = pad + (i + 0.5) * pitch;
        const px1 = cx - bw / 2, py1 = cy - bh / 2;
        if (m === 0) {
          x.fillStyle = "#171a25";
          x.fillRect(px1, py1, bw, bh);
          x.fillStyle = "#10121b";
          x.fillRect(cx - bw * 0.22, cy - bh * 0.22, bw * 0.44, bh * 0.44);
        } else {
          const dr = map[r], dg = map[g], db = map[b], k = map[m] / 255, wh = k * k * k * 0.5;
          const a = Math.min(0.85, 0.12 + k * 0.9);   // package body takes up the LED's color
          x.fillStyle = "rgb(" + ((23 + (dr - 23) * a) | 0) + "," + ((26 + (dg - 26) * a) | 0) + "," + ((37 + (db - 37) * a) | 0) + ")";
          x.fillRect(px1, py1, bw, bh);
          x.fillStyle = "rgb(" + ((dr + (255 - dr) * wh) | 0) + "," + ((dg + (255 - dg) * wh) | 0) + "," + ((db + (255 - db) * wh) | 0) + ")";
          x.fillRect(cx - bw * 0.34, cy - bh * 0.34, bw * 0.68, bh * 0.68);
        }
      }
      if (big && pin !== null) {
        x.strokeStyle = "#fff"; x.lineWidth = 1.5;
        x.strokeRect(pad + pin * pitch + pitch / 2 - bw / 2 - 2.5, cy - bh / 2 - 2.5, bw + 5, bh + 5);
      }
      this.bx.clearRect(0, 0, this.bloom.width, this.bloom.height);
      this.bx.drawImage(this.core, 0, 0);
      if (this.hx) { this.hx.clearRect(0, 0, this.halo.width, this.halo.height); this.hx.drawImage(this.core, 0, 0); }
    }
  }

  let detail = null, rows = [], hist = null, hx = null, histVis = null, hvx = null, colImg = null;
  const laneBuf = { v: new Float32Array(HISTN * 4), beat: new Uint8Array(HISTN), silent: new Uint8Array(HISTN), ok: new Uint8Array(HISTN) };

  function buildUi(infoMsg) {
    S.modes = infoMsg.modes; S.themes = infoMsg.themes; S.n = infoMsg.nLeds;
    $("cN").textContent = String(S.n);
    const mb = $("modes"), ar = $("allRows");
    S.modes.forEach((name, m) => {
      const b = el("button"); b.type = "button"; b.setAttribute("role", "radio"); b.dataset.m = String(m);
      const k = el("kbd"); k.textContent = String(m + 1);
      b.append(k, document.createTextNode(name));
      b.addEventListener("click", () => selectMode(m));
      mb.append(b);
      const r = el("button", "arow"); r.type = "button"; r.dataset.m = String(m);
      const nm = el("span", "nm"); const k2 = el("kbd"); k2.textContent = String(m + 1); nm.append(k2, document.createTextNode(name));
      const sh = el("div", "strip small");
      r.append(nm, sh);
      r.addEventListener("click", () => selectMode(m));
      ar.append(r);
      rows.push({ btn: r, strip: new Strip(sh, false) });
    });
    detail = new Strip($("detailStrip"), true);
    detail.redraw = () => { drawTicks(); if (S.last) drawStrips(S.last); };
    detail.host.addEventListener("pointerdown", (e) => { S.pin = detail.ledAt(e.clientX); if (S.last) drawStrips(S.last); });
    const tb = $("themes");
    S.themes.forEach((name, t) => {
      const b = el("button"); b.type = "button"; b.setAttribute("role", "radio"); b.dataset.v = String(t); b.textContent = name;
      b.addEventListener("click", () => setTheme(t));
      tb.append(b);
    });
    hist = el("canvas"); hist.width = HISTN; hist.height = S.n; hx = hist.getContext("2d");
    histVis = $("hist"); histVis.width = HISTN; histVis.height = S.n; hvx = histVis.getContext("2d");
    colImg = new ImageData(1, S.n);
    clearHistory();
    syncSelection();
  }

  function drawTicks() {
    const t = $("ticks"), pad = 16;
    t.textContent = "";
    if (!detail || !detail.w) return;
    const pitch = detail.pitch(), mk = (i) => { const s = el("span"); s.textContent = String(i); s.style.left = (pad + (i + 0.5) * pitch) + "px"; t.append(s); };
    for (let i = 0; i < S.n; i += 10) mk(i);
    mk(S.n - 1);
  }

  // ------------------------------------------------------------------ history and lanes
  function clearHistory() {
    hx.fillStyle = "#040509"; hx.fillRect(0, 0, HISTN, S.n);
    laneBuf.ok.fill(0); laneBuf.beat.fill(0); laneBuf.silent.fill(0);
  }
  function writeColumn(frame, px, off) {
    const map = S.view === "glow" ? GAMMA : IDENT, d = colImg.data;
    for (let i = 0; i < S.n; i++) { const o = off + i * 3, q = i * 4; d[q] = map[px[o]]; d[q + 1] = map[px[o + 1]]; d[q + 2] = map[px[o + 2]]; d[q + 3] = 255; }
    hx.putImageData(colImg, frame % HISTN, 0);
  }
  function pushHistory(r) {
    const n = S.n;
    if (r.seeked) clearHistory();
    for (let i = 0; i < r.count; i++) {
      const f = r.frame - r.count + i + 1, x = f % HISTN, h = i * HF;
      writeColumn(f, r.histLed, i * n * 3);
      laneBuf.v[x * 4] = r.hist[h] / 65535; laneBuf.v[x * 4 + 1] = r.hist[h + 1] / 65535;
      laneBuf.v[x * 4 + 2] = r.hist[h + 2] / 65535; laneBuf.v[x * 4 + 3] = r.hist[h + 3] / 65535;
      laneBuf.beat[x] = r.hist[h + 5]; laneBuf.silent[x] = r.hist[h + 7]; laneBuf.ok[x] = 1;
    }
    writeColumn(r.frame, r.leds, S.sel * n * 3);
    const x = r.frame % HISTN, s = S_SC;
    laneBuf.v[x * 4] = r.snap[s + SC.BASS] / 65535; laneBuf.v[x * 4 + 1] = r.snap[s + SC.MID] / 65535;
    laneBuf.v[x * 4 + 2] = r.snap[s + SC.TREBLE] / 65535; laneBuf.v[x * 4 + 3] = r.snap[s + SC.LOUD] / 65535;
    laneBuf.beat[x] = r.snap[s + SC.BEAT]; laneBuf.silent[x] = r.snap[s + SC.SILENT]; laneBuf.ok[x] = 1;
    S.head = x;
  }
  function drawHistory() {
    const head = S.head, n = S.n;
    if (head < HISTN - 1) hvx.drawImage(hist, head + 1, 0, HISTN - head - 1, n, 0, 0, HISTN - head - 1, n);
    hvx.drawImage(hist, 0, 0, head + 1, n, HISTN - head - 1, 0, head + 1, n);
    hvx.fillStyle = "rgba(255,255,255,.85)";
    hvx.fillRect(HISTN - 1, 0, 1, n);
  }
  const LANES = [["Bass", "hsl(14 90% 60%)"], ["Mid", "hsl(145 70% 55%)"], ["Treble", "hsl(268 85% 70%)"], ["Loudness", "hsl(190 90% 70%)"]];
  function drawLanes() {
    const [x, w, h] = fit($("lanes"));
    x.clearRect(0, 0, w, h);
    const lh = h / 4, step = w / HISTN;
    for (let k = 0; k < HISTN; k++) {
      const i = (S.head + 1 + k) % HISTN;
      if (!laneBuf.ok[i]) continue;
      if (laneBuf.silent[i]) { x.fillStyle = "rgba(255,255,255,.05)"; x.fillRect(k * step, 0, step + 0.5, h); }
      if (laneBuf.beat[i]) { x.fillStyle = "rgba(255,255,255,.28)"; x.fillRect(k * step, 0, Math.max(1, step), h); }
    }
    LANES.forEach(([name, col], l) => {
      const y1 = (l + 1) * lh - 2, amp = lh - 6;
      x.beginPath(); x.moveTo(0, y1);
      let started = false;
      for (let k = 0; k < HISTN; k++) {
        const i = (S.head + 1 + k) % HISTN;
        if (!laneBuf.ok[i]) continue;
        x.lineTo(k * step, y1 - Math.min(1, laneBuf.v[i * 4 + l]) * amp); started = true;
      }
      if (started) { x.lineTo(w, y1); x.closePath(); x.fillStyle = col.replace(")", " / .28)"); x.fill(); x.strokeStyle = col; x.lineWidth = 1; x.stroke(); }
      x.fillStyle = "rgba(139,146,169,.9)"; x.font = "10.5px ui-monospace, monospace"; x.textBaseline = "top";
      x.fillText(name, 6, l * lh + 2);
      x.fillStyle = "rgba(255,255,255,.06)"; x.fillRect(0, (l + 1) * lh - 1, w, 1);
    });
  }

  // ------------------------------------------------------------------ drawing one reply
  function drawStrips(r) {
    const map = S.view === "glow" ? GAMMA : IDENT, n = S.n;
    for (let m = 0; m < 9; m++) {
      const live = S.allModes || m === S.sel;
      rows[m].btn.classList.toggle("off", !live);
      if (live) rows[m].strip.draw(r.leds, m * n * 3, map, null);
    }
    detail.draw(r.leds, S.sel * n * 3, map, S.pin);
    // ambient light of the selected mode: average wire color
    let ar = 0, ag = 0, ab = 0, o = S.sel * n * 3;
    for (let i = 0; i < n; i++) { ar += GAMMA[r.leds[o + i * 3]]; ag += GAMMA[r.leds[o + i * 3 + 1]]; ab += GAMMA[r.leds[o + i * 3 + 2]]; }
    const lum = Math.max(ar, ag, ab) / (255 * n);
    $("stage").style.setProperty("--wash", "rgba(" + ((ar / n) | 0) + "," + ((ag / n) | 0) + "," + ((ab / n) | 0) + "," + Math.min(0.5, lum * 1.4).toFixed(3) + ")");
  }

  function drawReadouts(r) {
    const s = r.snap, n = S.n, o = S.sel * n * 3, st = r.stats, k = S.sel * 5;
    const bar = (id, v) => { const e = $(id); const w = (Math.min(1, v / 65535) * 100).toFixed(1) + "%"; if (e.style.width !== w) e.style.width = w; };
    bar("mBass", s[S_SC + SC.BASS]); bar("mMid", s[S_SC + SC.MID]); bar("mTreble", s[S_SC + SC.TREBLE]); bar("mLoud", s[S_SC + SC.LOUD]);
    const env = s[S_SC + SC.BEATENV] / 65535, bpm = s[S_SC + SC.BPM];
    $("beatDot").style.background = env > 0.02 ? "hsl(190 90% " + (35 + env * 35).toFixed(0) + "%)" : "";
    $("beatDot").style.boxShadow = env > 0.02 ? "0 0 " + (14 * env).toFixed(1) + "px hsl(190 90% 62% / " + env.toFixed(2) + ")" : "none";
    setText($("bpm"), bpm ? bpm + " BPM" : "–");
    setText($("silent"), s[S_SC + SC.SILENT] ? "idle (silent)" : "sound");
    setText($("ceil"), (s[S_SC + SC.CEILING] / DBU).toFixed(1));
    setText($("ma"), String(st[k])); setText($("budget"), String(st[k + 4]));
    setText($("limiter"), st[k + 1] >= 255 ? "not limiting" : "limiting " + Math.round(100 - st[k + 1] * 100 / 255) + " %");
    setText($("frameNo"), String(r.frame));
    let off = 0, dim = 0, lit = 0;
    for (let i = 0; i < n; i++) { const a = r.leds[o + i * 3], b = r.leds[o + i * 3 + 1], c = r.leds[o + i * 3 + 2], m = a > b ? (a > c ? a : c) : (b > c ? b : c); if (m === 0) off++; else if (m < 16) dim++; else lit++; }
    setText($("cOff"), String(off)); setText($("cDim"), String(dim)); setText($("cLit"), String(lit));
    let sub = "";
    if (S.pin !== null) sub = "LED " + S.pin + " = " + r.leds[o + S.pin * 3] + ", " + r.leds[o + S.pin * 3 + 1] + ", " + r.leds[o + S.pin * 3 + 2] + " (wire R, G, B)";
    else if (S.sel === 8) sub = "playing " + S.modes[st[k + 2]] + " · click an LED to inspect it";
    else sub = S.n + " LEDs · click an LED to inspect it";
    setText($("modeSub"), sub);
  }

  const scrCtx = $("screen").getContext("2d", { alpha: false });
  function drawScreen(r) {
    if (!r.screen) return;
    scrCtx.putImageData(new ImageData(new Uint8ClampedArray(r.screen.buffer), 240, 240), 0, 0);
  }

  function onFrame(r) {
    const t0 = performance.now();
    S.inflight = false;
    S.shown = r.frame;
    S.last = r;
    pushHistory(r);
    drawStrips(r);
    drawReadouts(r);
    drawHistory();
    drawLanes();
    drawScreen(r);
    S.ms = S.ms * 0.9 + r.ms * 0.1;
    S.drawMs = S.drawMs * 0.9 + (performance.now() - t0) * 0.1;
    if (t0 - S.perfAt > 400) { S.perfAt = t0; setText($("perf"), "core " + S.ms.toFixed(2) + " ms · drawing " + S.drawMs.toFixed(1) + " ms · " + S.backend); }
  }

  // ------------------------------------------------------------------ controls
  function syncSelection() {
    [...$("modes").children].forEach((b) => b.setAttribute("aria-checked", String(+b.dataset.m === S.sel)));
    rows.forEach((r, m) => r.btn.setAttribute("aria-current", String(m === S.sel)));
    setText($("modeTitle"), S.modes[S.sel]);
    setText($("modeText"), MODE_TEXT[S.modes[S.sel]] || "");
    [...$("themes").children].forEach((b) => b.setAttribute("aria-checked", String(+b.dataset.v === S.theme)));
  }
  function selectMode(m) {
    if (m === S.sel) return;
    S.sel = m; S.pin = null;
    syncSelection();
    send({ type: "config", sel: m });
    clearHistory();
    S.needFrame = true;
  }
  function setTheme(t) { S.theme = t; send({ type: "param", name: "theme", value: t }); syncSelection(); S.needFrame = true; }
  const paramChanged = () => { if (audio.paused) S.force = true; };
  function setSens(v) {
    S.sens = v;
    [...$("sens").children].forEach((b) => b.setAttribute("aria-checked", String(+b.dataset.v === v)));
    send({ type: "param", name: "agcmin", value: v });
    paramChanged();
  }
  function setBri(v) {
    S.bri = Math.max(0, Math.min(255, v));
    $("bri").value = String(S.bri); setText($("briVal"), String(S.bri));
    send({ type: "param", name: "bri", value: S.bri });
    paramChanged();
  }
  function setScope(all) {
    S.allModes = all;
    [...$("analyze").children].forEach((b) => b.setAttribute("aria-checked", String((b.dataset.v === "all") === all)));
    send({ type: "config", allModes: all });
    setText($("allHint"), all ? "· click a row to select it" : "· only the selected mode is computed");
    S.needFrame = true;
  }
  function setView(v) {
    S.view = v;
    [...$("view").children].forEach((b) => b.setAttribute("aria-checked", String(b.dataset.v === v)));
    for (const s of [detail, ...rows.map((r) => r.strip)]) s.host.classList.toggle("glowoff", v === "raw");
    if (S.last) { drawStrips(S.last); }
  }
  function setScreen(on) {
    S.screenOn = on;
    send({ type: "config", screen: on });
    $("screen").style.opacity = on ? "1" : "0.25";
    if (!on) scrCtx.fillRect(0, 0, 240, 240);
    S.needFrame = true;
  }

  function wire() {
    $("pick").addEventListener("click", () => $("file").click());
    $("file").addEventListener("change", (e) => { const f = e.target.files[0]; e.target.value = ""; loadFile(f); });
    $("demoBtn").addEventListener("click", () => {
      if (S.busy) return;
      S.busy = true; audio.pause(); info("Building the demo …");
      send({ type: "demo" });
    });
    const drop = $("drop");
    const stop = (e) => { e.preventDefault(); };
    window.addEventListener("dragover", (e) => { stop(e); drop.classList.add("over"); });
    window.addEventListener("dragleave", (e) => { if (!e.relatedTarget) drop.classList.remove("over"); });
    window.addEventListener("drop", (e) => { stop(e); drop.classList.remove("over"); loadFile(e.dataTransfer.files[0]); });
    $("play").addEventListener("click", togglePlay);
    $("seek").addEventListener("pointerdown", () => { S.dragging = true; });
    window.addEventListener("pointerup", () => { S.dragging = false; });
    $("seek").addEventListener("input", (e) => seekTo(+e.target.value));
    $("speed").addEventListener("change", (e) => { audio.playbackRate = +e.target.value; });
    $("vol").addEventListener("input", (e) => { audio.volume = +e.target.value; });
    $("loop").addEventListener("change", (e) => { audio.loop = e.target.checked; });
    [...$("sens").children].forEach((b) => b.addEventListener("click", () => setSens(+b.dataset.v)));
    $("bri").addEventListener("input", (e) => setBri(+e.target.value));
    [...$("analyze").children].forEach((b) => b.addEventListener("click", () => setScope(b.dataset.v === "all")));
    [...$("view").children].forEach((b) => b.addEventListener("click", () => setView(b.dataset.v)));
    $("screenOn").addEventListener("change", (e) => setScreen(e.target.checked));
    document.addEventListener("visibilitychange", () => { S.needFrame = true; });
    document.addEventListener("keydown", (e) => {
      if (e.ctrlKey || e.metaKey || e.altKey) return;
      const t = e.target, isRange = t.tagName === "INPUT" && t.type === "range", ownsSpace = /^(BUTTON|SELECT)$/.test(t.tagName) || (t.tagName === "INPUT" && t.type === "checkbox");
      const k = e.key;
      if (k === " ") { if (ownsSpace) return; e.preventDefault(); togglePlay(); }
      else if (k === "ArrowLeft" || k === "ArrowRight") { if (isRange) return; e.preventDefault(); seekTo(audio.currentTime + (k === "ArrowRight" ? 1 : -1) * (e.shiftKey ? 30 : 5)); }
      else if (k === "ArrowUp" || k === "ArrowDown") { if (isRange) return; e.preventDefault(); setBri(S.bri + (k === "ArrowUp" ? 16 : -16)); }
      else if (k >= "1" && k <= "9") { if (t.tagName === "SELECT") return; selectMode(+k - 1); }
      else if (k === "s" || k === "S") { const v = [-45, -52, -60]; setSens(v[(v.indexOf(S.sens) + 1) % 3]); }
      else if (k === "t" || k === "T") setTheme((S.theme + 1) % S.themes.length);
    });
  }

  // ------------------------------------------------------------------ main loop
  function tick(now) {
    requestAnimationFrame(tick);
    if (!S.loaded) return;
    const t = audio.currentTime, target = Math.min(S.frames, Math.floor(t * FPS + 1e-6));
    if (now - S.clockAt > 66) {
      S.clockAt = now;
      setText($("tNow"), fmt(t));
      if (!S.dragging) $("seek").value = String(t);
    }
    if (!S.inflight && (target !== S.shown || S.needFrame || S.force)) {
      S.inflight = true;
      send({ type: "run", to: target, force: S.force });
      S.force = false; S.needFrame = false;
    }
  }

  function onReady(infoMsg) {
    S.ready = true;
    buildUi(infoMsg);
    setBri(infoMsg.lim.bri); S.sens = infoMsg.lim.agcmin;
    send({ type: "config", sel: 0, allModes: true, screen: true });
    $("pick").disabled = false; $("demoBtn").disabled = false;
    if (!(window.OfflineAudioContext || window.webkitOfflineAudioContext)) info("This browser cannot decode audio files; only the built-in demo is available.");
    S.busy = true;
    send({ type: "demo" });
    info("Building the demo …");
  }

  wire();
  requestAnimationFrame(tick);
  startBackend();
})();
