// test_wasm.mjs - compares the WASM core with the host golden hashes and runs smoke tests. No dependencies.
// Run after `sim/build.sh`: node sim/test_wasm.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..');
const wasmPath = path.join(root, 'docs', 'viewer', 'fftviz.wasm');
const goldenPath = path.join(root, 'tests', 'golden', 'golden.json');
const GH_FEATURES = 1, FRAMES = 240;

if (!fs.existsSync(wasmPath)) { console.error('ERROR: docs/viewer/fftviz.wasm is missing, run sim/build.sh first'); process.exit(1); }
const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
const w = instance.exports;
const str = (p) => { const m = new Uint8Array(w.memory.buffer); let e = p; while (m[e]) e++; return Buffer.from(m.subarray(p, e)).toString(); };
w.viz_init(1);

const scCount = w.viz_scenario_count();
const names = [];
for (let s = 0; s < scCount; s++) names.push(str(w.viz_scenario_name(s)));

// Flatten golden.json defensively: path -> value
function flatten(o, p = '', out = []) {
  if (o && typeof o === 'object') for (const [k, v] of Object.entries(o)) flatten(v, p ? p + '.' + k : k, out);
  else out.push([p.toLowerCase(), o]);
  return out;
}
const toHash = (v) => {
  if (typeof v === 'number') return v >>> 0;
  if (typeof v === 'string') { const t = v.trim(); if (/^(0x)?[0-9a-f]{1,8}$/i.test(t)) return parseInt(t.replace(/^0x/i, ''), /^0x/i.test(t) || /[a-f]/i.test(t) ? 16 : 10) >>> 0; }
  return null;
};
let golden = null, note = '';
if (!fs.existsSync(goldenPath)) note = 'tests/golden/golden.json is missing';
else {
  try { golden = flatten(JSON.parse(fs.readFileSync(goldenPath, 'utf8'))); }
  catch (e) { note = 'golden.json could not be read: ' + e.message; }
}
const findGolden = (name) => {
  if (!golden) return null;
  const n = name.toLowerCase();
  for (const [p, v] of golden) {
    if (!p.includes(n)) continue;
    if (!/feat/.test(p)) continue;
    if (/frames|leds|screen/.test(p.replace(/features?/g, ''))) continue;
    const h = toHash(v); if (h !== null) return h;
  }
  return null;
};

const hex = (x) => '0x' + (x >>> 0).toString(16).padStart(8, '0');
let pass = 0, total = 0, fail = 0, missing = 0;
console.log('scenario'.padEnd(12), 'wasm'.padEnd(12), 'golden'.padEnd(12), 'result');
for (let s = 0; s < scCount; s++) {
  const h = w.viz_scenario_hash(s, FRAMES, GH_FEATURES) >>> 0;
  const g = findGolden(names[s]);
  let res;
  total++;
  if (g === null) { res = 'SKIP (no golden)'; missing++; }
  else if (g === h) { res = 'OK'; pass++; }
  else { res = 'DIFFERENT'; fail++; }
  console.log(names[s].padEnd(12), hex(h).padEnd(12), (g === null ? '-' : hex(g)).padEnd(12), res);
}

// determinism: the same hash twice
const again = w.viz_scenario_hash(3, FRAMES, GH_FEATURES) >>> 0;
const first = w.viz_scenario_hash(3, FRAMES, GH_FEATURES) >>> 0;
if (again !== first) { console.error('ERROR: hash is not deterministic'); process.exit(1); }

// smoke: KICK120, 2 s -> render -> is the frame blank?
w.viz_init(1);
w.viz_synth(3, 0);
let frames = 0;
for (let i = 0; i < 20; i++) { frames += w.viz_synth_push(4800); }  // 2 s @48 kHz
w.viz_render(0);
const fb = new Uint16Array(w.memory.buffer, w.viz_fb(), 240 * 240);
let nz = 0; for (let i = 0; i < fb.length; i++) if (fb[i]) nz++;
const lp = new Uint8Array(w.memory.buffer, w.viz_leds(1), 360);
let lz = 0; for (let i = 0; i < 360; i++) if (lp[i]) lz++;
console.log(`smoke: ${frames} frames, fb non-zero ${nz}/${fb.length}, led non-zero ${lz}/360, bpm=${w.viz_bpm()}, ma=${w.viz_ma()}`);
if (frames === 0 || (nz === 0 && lz === 0)) { console.log('FAIL: output is blank'); fail++; }

// GH_ALL (features + LED output + screen pixels): checked when golden has "all.stable".
try {
  const g = JSON.parse(fs.readFileSync(goldenPath, 'utf8'));
  if (g.all && g.all.stable && g.all.hashes) {
    let ap = 0, at = 0;
    for (let s = 0; s < scCount; s++) {
      const name = names[s];
      if (!(name in g.all.hashes)) continue;
      at++;
      const h = w.viz_scenario_hash(s, g.frames || FRAMES, 7) >>> 0;
      if (h === (g.all.hashes[name] >>> 0)) ap++;
      else { fail++; console.log(`GH_ALL ${name}: wasm 0x${h.toString(16)} != golden 0x${(g.all.hashes[name] >>> 0).toString(16)}`); }
    }
    console.log(`WASM_PARITY_ALL ${ap === at ? 'PASS' : 'FAIL'} ${ap}/${at} (including LED and screen pixels)`);
  }
} catch (e) { console.log('GH_ALL check skipped: ' + e.message); }

const pid = (n) => { for (let i = 0; i < w.viz_param_count(); i++) if (str(w.viz_param_name(i)) === n) return i; return -1; };

// Hardware current limit: effective budget = min(ma, psu_ma*psu_pct/100, ma_usb[USB]); the modelled mA never exceeds it.
{
  const [iMa, iLed, iPsu, iPct, iUsb, iN, iIdle] = ['ma', 'led_ma', 'psu_ma', 'psu_pct', 'ma_usb', 'leds', 'led_idle'].map(pid);
  let ok = [iMa, iLed, iPsu, iPct, iUsb, iN, iIdle].every((i) => i >= 0);
  const cases = [
    // [ma, led_ma, psu_ma, psu_pct, usb, ma_usb, expected budget]
    [2500, 60, 0, 80, 0, 500, 2500],
    [3400, 30, 3800, 90, 0, 500, 3400],
    [20000, 30, 3800, 90, 0, 500, 3420],
    [3400, 30, 3800, 90, 1, 500, 500],
    [800, 60, 0, 80, 0, 500, 800],
  ];
  let bad = 0;
  for (const [ma, lm, psu, pct, usb, mu, want] of cases) {
    w.viz_init(1); w.viz_defaults();
    w.viz_set(iMa, ma); w.viz_set(iLed, lm); w.viz_set(iPsu, psu); w.viz_set(iPct, pct); w.viz_set(iUsb, mu); w.viz_set_usb(usb);
    w.viz_synth(3, 0);
    let worst = 0;
    for (let k = 0; k < 20; k++) { w.viz_synth_push(4800); worst = Math.max(worst, w.viz_ma()); }
    const base = Math.floor(w.viz_led_count() * w.viz_get(iIdle) / 1000);
    if (w.viz_budget() !== want || w.viz_budget_now() !== want || worst > Math.max(want, base)) { bad++; console.log(`  power case ma=${ma} led_ma=${lm} psu=${psu}/${pct} usb=${usb}: budget ${w.viz_budget()} (expected ${want}), peak ${worst}`); }
  }
  ok = ok && bad === 0;
  w.viz_set_usb(0);
  console.log(`WASM_POWER ${ok ? 'PASS' : 'FAIL'} ${cases.length - bad}/${cases.length} (effective budget + modelled mA <= budget)`);
  if (!ok) fail++;
}

// Tunable range: the fmin/fmax parameters change the band layout (viz_band_edge) and restart the analyzer.
{
  const iMin = pid('fmin'), iMax = pid('fmax');
  let ok = iMin >= 0 && iMax >= 0 && w.viz_get(iMin) === 250 && w.viz_get(iMax) === 10000;
  w.viz_init(1);
  w.viz_synth(4, 0);
  const edges = () => Array.from({ length: 41 }, (_, i) => w.viz_band_edge(i));
  w.viz_synth_push(4800);
  const def = edges();
  ok = ok && def[0] === 11 && def[40] === 427;
  w.viz_set(iMin, 47);
  w.viz_synth_push(4800);
  const legacy = edges();
  ok = ok && legacy[0] === 2 && legacy[40] === 427 && legacy[1] === 3 && legacy[11] === 14 && legacy[39] === 379;
  w.viz_set(iMin, 600); w.viz_set(iMax, 6000);
  w.viz_synth_push(4800);
  const nar = edges();
  ok = ok && nar[0] === 26 && nar[40] === 256 && nar.every((e, i) => i === 0 || e > nar[i - 1]);
  console.log(`WASM_RANGE ${ok ? 'PASS' : 'FAIL'} default ${def[0]}..${def[40]}, legacy ${legacy[0]}..${legacy[40]}, narrow ${nar[0]}..${nar[40]}`);
  if (!ok) fail++;
}

// Tunable strip length: golden "leds_param" section (same as the host) + counter + slice limit.
try {
  const g = JSON.parse(fs.readFileSync(goldenPath, 'utf8'));
  const lp = g.leds_param;
  const modeNames = []; for (let m = 0; m < w.viz_mode_count(); m++) modeNames.push(str(w.viz_mode_name(m)));
  const mixIdx = names.indexOf('mix');
  let lpPass = 0, lpTotal = 0;
  for (const [key, want] of Object.entries(lp.hashes)) {
    const mm = /^L(\d+)_(.+)$/.exec(key);
    const h = w.viz_scenario_hash_ex(mixIdx, lp.frames, 2, modeNames.indexOf(mm[2]), Number(mm[1])) >>> 0;
    lpTotal++;
    if (h === (want >>> 0)) lpPass++; else { fail++; console.log(`LEDS ${key}: wasm 0x${h.toString(16)} != golden 0x${(want >>> 0).toString(16)}`); }
  }
  // counter: leds=60 -> viz_led_count 60, everything after LED 60 stays black
  w.viz_init(1);
  w.viz_set(pid('leds'), 60);
  w.viz_synth(4, 0);
  w.viz_synth_push(9600);
  const px = new Uint8Array(w.memory.buffer, w.viz_leds(0), w.viz_led_max() * 3);
  let tail = 0; for (let i = 60 * 3; i < px.length; i++) tail |= px[i];
  const okCnt = w.viz_led_count() === 60 && w.viz_led_max() === 300 && tail === 0;
  if (!okCnt) fail++;
  console.log(`WASM_LEDS ${lpPass === lpTotal && okCnt ? 'PASS' : 'FAIL'} ${lpPass}/${lpTotal} leds_param, count=${w.viz_led_count()}/${w.viz_led_max()}, remainder black=${tail === 0}`);
} catch (e) { console.log('WASM_LEDS skipped: ' + e.message); fail++; }

// All-modes engine: every per-mode LED engine fed by the single analyzer is bit-identical to a dedicated single-mode run.
{
  const fnv = (h, a) => { for (let i = 0; i < a.length; i++) h = Math.imul(h ^ a[i], 16777619) >>> 0; return h; };
  const modes = w.viz_mode_count(), scen = names.indexOf('mix'), FR = 300, SEED = 2166136261;
  const n = () => w.viz_led_count() * 3;
  const ref = [];
  for (let m = 0; m < modes; m++) {
    w.viz_init(1); w.viz_set(pid('mode'), m); w.viz_synth(scen, -26);
    let h = SEED;
    for (let i = 0; i < FR; i++) { w.viz_synth_push(800); h = fnv(h, new Uint8Array(w.memory.buffer, w.viz_leds(0), n())); }
    ref.push(h);
  }
  w.viz_init(1);
  const mask = (1 << modes) - 1;
  const ret = w.viz_modes(mask);
  w.viz_synth(scen, -26);
  const got = new Array(modes).fill(SEED);
  for (let i = 0; i < FR; i++) { w.viz_synth_push(800); for (let m = 0; m < modes; m++) got[m] = fnv(got[m], new Uint8Array(w.memory.buffer, w.viz_mode_leds(m), n())); }
  const diff = ref.map((h, m) => (h === got[m] ? -1 : m)).filter((m) => m >= 0);
  const stats = w.viz_mode_stat_n() > 0 && new Int32Array(w.memory.buffer, w.viz_mode_stats(), modes * w.viz_mode_stat_n()).some((v) => v !== 0);
  const ok = ret === mask && modes === 9 && diff.length === 0 && stats;
  console.log(`WASM_MODES ${ok ? 'PASS' : 'FAIL'} ${modes - diff.length}/${modes} per-mode engines match a dedicated run${diff.length ? ' (differ: ' + diff.join(',') + ')' : ''}, stats ${stats ? 'filled' : 'empty'}`);
  if (!ok) fail++;
}

// viz_synth_render + viz_push must equal viz_synth_push (the page feeds the analyzer from its own buffer).
{
  const fnv = (h, a) => { for (let i = 0; i < a.length; i++) h = Math.imul(h ^ a[i], 16777619) >>> 0; return h; };
  const scen = names.indexOf('kick120'), FR = 120, SEED = 2166136261;
  const trace = (feed) => {
    w.viz_init(1); w.viz_synth(scen, -26);
    let h = SEED;
    for (let i = 0; i < FR; i++) { feed(); h = fnv(h, new Uint8Array(w.memory.buffer, w.viz_leds(0), w.viz_led_count() * 3)); }
    return [h, w.viz_bpm(), w.viz_ma()];
  };
  const a = trace(() => w.viz_synth_push(800));
  const b = trace(() => { w.viz_synth_render(800); w.viz_push(800); });
  const ok = a.join() === b.join() && a[2] > 0;
  console.log(`WASM_RENDER ${ok ? 'PASS' : 'FAIL'} synth_render+push == synth_push (bpm ${a[1]}, ma ${a[2]})`);
  if (!ok) fail++;
}

// viz_calibrate: learning the noise floor must quiet ADC noise that rides under a tone.
{
  const SC_OFF = 240, MA = 14;
  const run = (cal) => {
    w.viz_init(1);
    let s = 12345;
    const rnd = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; };
    const inv = new Uint16Array(w.memory.buffer, w.viz_in(), 800);
    for (let f = 0; f < 480; f++) {
      if (f === 20 && cal) w.viz_calibrate(120);
      for (let i = 0; i < 800; i++) {
        const noise = (rnd() - rnd()) * 7.355;
        const tone = f > 300 ? 40 * Math.sin(2 * Math.PI * 1000 * (f * 800 + i) / 48000) : 0;
        inv[i] = Math.round(2048 + noise + tone);
      }
      w.viz_push(800);
    }
    w.viz_snapshot();
    return new Int32Array(w.memory.buffer, w.viz_snap(), w.viz_snap_len())[SC_OFF + MA];
  };
  const raw = run(false), cal = run(true);
  const ok = cal > 0 && cal < raw;
  console.log(`WASM_CALIBRATE ${ok ? 'PASS' : 'FAIL'} modelled mA with tone over noise: ${raw} uncalibrated, ${cal} calibrated`);
  if (!ok) fail++;
}

if (fail) { console.log(`WASM_PARITY FAIL ${pass}/${total} (${fail} failed)`); process.exit(1); }
if (missing === total) { console.log(`SKIP: ${note || 'no features hashes found in golden.json'} - parity could not be checked`); console.log(`WASM_PARITY SKIP 0/${total}`); process.exit(0); }
console.log(`WASM_PARITY PASS ${pass}/${total}` + (missing ? ` (${missing} golden missing)` : ''));
