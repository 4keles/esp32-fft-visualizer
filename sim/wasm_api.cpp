// wasm_api.cpp — fvcore için WASM C ABI. Yalnızca statik buffer, heap yok.
#include "fftviz.h"

inline void* operator new(size_t, void* p) noexcept { return p; }  // <new> yok; placement new

#define EXPORT(name) extern "C" __attribute__((export_name(#name), used))

namespace {

constexpr int IN_SAMPLES = 4096;
constexpr int SNAP_LEN = 6 * fv::NUM_BANDS + 32;

// Nesneler ham depolamada tutulur: statik yapıcı (global ctor) çalışmaz. wasm-ld "command" modülünde
// her export çağrısından önce __wasm_call_ctors çalıştırır; varsayılan üye ilklendiricileri durumu sıfırlardı.
alignas(16) uint8_t g_engMem[sizeof(fv::Engine)];
alignas(16) uint8_t g_synthMem[sizeof(fv::Synth)];
alignas(16) uint8_t g_scrMem[sizeof(fv::ScreenRenderer)];
#define g_eng (*reinterpret_cast<fv::Engine*>(g_engMem))
#define g_synth (*reinterpret_cast<fv::Synth*>(g_synthMem))
#define g_scr (*reinterpret_cast<fv::ScreenRenderer*>(g_scrMem))

uint16_t g_in[IN_SAMPLES];
uint16_t g_fb[fv::SCREEN_W * fv::SCREEN_H];
int32_t g_snap[SNAP_LEN];

uint32_t g_seed = 1;
uint32_t g_tick = 0;
uint32_t g_hudUntil = 0;     // HUD'ın açık kalacağı son tick
uint8_t g_hudTheme = 0;      // son değişiklik temaysa tema adı gösterilir
uint16_t g_fps10 = 600;

// One LedEngine per LED mode, all fed by the single analyzer of g_eng. Engine m runs with its own Params copy whose
// P_LED_MODE is m; every other parameter follows g_eng.params. Only modes set in g_modeMask are stepped.
constexpr int MODE_STAT_N = 5;  // per mode: mA, limiter Q8, active (sub)mode, demand mA, budget mA
alignas(16) uint8_t g_modeMem[fv::LM_COUNT][sizeof(fv::LedEngine)];
fv::Params g_modeParams[fv::LM_COUNT];
int32_t g_modeStat[fv::LM_COUNT * MODE_STAT_N];
uint32_t g_modeMask = 0;
#define MODE_LED(m) (*reinterpret_cast<fv::LedEngine*>(g_modeMem[m]))

void syncModeParams() {
  for (int m = 0; m < fv::LM_COUNT; ++m) {
    g_modeParams[m] = g_eng.params;
    g_modeParams[m].v[fv::P_LED_MODE] = m;
  }
}

void resetMode(int m) { MODE_LED(m).reset(g_seed, g_eng.params.get(fv::P_LEDS)); }

void stepModes() {
  const fv::Features& f = g_eng.features();
  for (int m = 0; m < fv::LM_COUNT; ++m) {
    if (!(g_modeMask >> m & 1u)) continue;
    fv::LedEngine& e = MODE_LED(m);
    e.step(f, g_modeParams[m]);
    int32_t* s = g_modeStat + m * MODE_STAT_N;
    s[0] = e.milliamps();
    s[1] = e.limiterQ8();
    s[2] = e.activeMode();
    s[3] = e.demandMa();
    s[4] = e.budgetMa();
  }
}

// Snapshot düzeni (JS ile paylaşılır): [0..39] level, bar, cap, led, raw, noise; sonra skalerler.
enum : int {
  S_LEVEL = 0, S_BAR = 40, S_CAP = 80, S_LED = 120, S_RAW = 160, S_NOISE = 200,
  S_SC = 240,  // skaler başlangıcı
  SC_FRAME = 0, SC_BEAT, SC_ONSET, SC_SILENT, SC_HUE, SC_BPM, SC_BEATENV, SC_BEATSTR,
  SC_BASS, SC_MID, SC_TREBLE, SC_LOUD, SC_CEILING, SC_BFP, SC_MA, SC_LIMITER,
  SC_MODE, SC_CLIP, SC_DC, SC_DEMAND, SC_BUDGET, SC_COUNT
};

void fillSnapshot() {
  const fv::Features& f = g_eng.features();
  for (int i = 0; i < fv::NUM_BANDS; ++i) {
    g_snap[S_LEVEL + i] = f.level[i];
    g_snap[S_BAR + i] = f.bar[i];
    g_snap[S_CAP + i] = f.cap[i];
    g_snap[S_LED + i] = f.led[i];
    g_snap[S_RAW + i] = f.raw[i];
    g_snap[S_NOISE + i] = f.noise[i];
  }
  int32_t* s = g_snap + S_SC;
  s[SC_FRAME] = (int32_t)f.frame;
  s[SC_BEAT] = f.beat;
  s[SC_ONSET] = f.onset;
  s[SC_SILENT] = f.silent;
  s[SC_HUE] = f.centroidHue;
  s[SC_BPM] = f.bpm;
  s[SC_BEATENV] = f.beatEnv;
  s[SC_BEATSTR] = f.beatStrength;
  s[SC_BASS] = f.bass;
  s[SC_MID] = f.mid;
  s[SC_TREBLE] = f.treble;
  s[SC_LOUD] = f.loud;
  s[SC_CEILING] = f.ceiling;
  s[SC_BFP] = f.bfpShift;
  s[SC_MA] = g_eng.leds().milliamps();
  s[SC_LIMITER] = g_eng.leds().limiterQ8();
  s[SC_MODE] = g_eng.leds().activeMode();
  s[SC_CLIP] = (int32_t)g_eng.decim().clipCount();
  s[SC_DC] = g_eng.decim().dcRaw();
  s[SC_DEMAND] = g_eng.leds().demandMa();
  s[SC_BUDGET] = g_eng.leds().budgetMa();
}

int pushChunked(const uint16_t* raw, int n) {
  int frames = 0;
  n &= ~1;
  for (int off = 0; off < n;) {
    int c = n - off;
    if (c > fv::ADC_BLOCK) c = fv::ADC_BLOCK;
    const int k = g_eng.pushAdc(raw + off, c);  // c <= ADC_BLOCK: at most one frame per call
    if (k && g_modeMask) stepModes();
    frames += k;
    off += c;
  }
  return frames;
}

const fv::Features& feat() { return g_eng.features(); }
int clampBand(int i) { return i < 0 ? 0 : (i >= fv::NUM_BANDS ? fv::NUM_BANDS - 1 : i); }

}  // namespace

EXPORT(viz_init) void viz_init(uint32_t seed) {
  g_seed = seed ? seed : 1;
  new (g_engMem) fv::Engine();
  new (g_synthMem) fv::Synth();
  new (g_scrMem) fv::ScreenRenderer();
  g_eng.params.defaults();
  g_eng.reset(g_seed);
  syncModeParams();
  g_modeMask = 0;
  for (int i = 0; i < fv::LM_COUNT * MODE_STAT_N; ++i) g_modeStat[i] = 0;
  for (int m = 0; m < fv::LM_COUNT; ++m) {
    new (g_modeMem[m]) fv::LedEngine();
    resetMode(m);
  }
  g_synth.reset(fv::SC_SILENCE, g_seed, 0);
  g_tick = 0;
  g_hudUntil = 0;
  for (int i = 0; i < IN_SAMPLES; ++i) g_in[i] = 2048;
}

EXPORT(viz_reset) void viz_reset(uint32_t seed) {
  g_seed = seed ? seed : 1;
  g_eng.reset(g_seed);  // params korunur
  for (int m = 0; m < fv::LM_COUNT; ++m) resetMode(m);
}

EXPORT(viz_in) uint16_t* viz_in() { return g_in; }
EXPORT(viz_in_len) int viz_in_len() { return IN_SAMPLES; }

EXPORT(viz_push) int viz_push(int n) {
  if (n > IN_SAMPLES) n = IN_SAMPLES;
  if (n <= 0) return 0;
  return pushChunked(g_in, n);
}

EXPORT(viz_synth) void viz_synth(int sc, int gainDb) {
  if (sc < 0 || sc >= fv::SC_COUNT) sc = fv::SC_SILENCE;
  if (gainDb > 0) gainDb = 0;
  if (gainDb < -90) gainDb = -90;
  g_synth.reset((fv::Scenario)sc, g_seed, (int8_t)gainDb);
}

EXPORT(viz_synth_push) int viz_synth_push(int n) {
  int frames = 0;
  n &= ~1;
  while (n > 0) {
    int c = n > IN_SAMPLES ? IN_SAMPLES : n;
    g_synth.render(g_in, c);
    frames += pushChunked(g_in, c);
    n -= c;
  }
  return frames;
}

// Render n samples of the current synth scenario into viz_in() without analyzing them.
EXPORT(viz_synth_render) int viz_synth_render(int n) {
  if (n > IN_SAMPLES) n = IN_SAMPLES;
  if (n <= 0) return 0;
  g_synth.render(g_in, n);
  return n;
}

// Learn the noise floor over the next `frames` analysis frames (the `cal` command).
EXPORT(viz_calibrate) void viz_calibrate(int frames) { g_eng.analyzer().calibrateNoise(frames); }

// Refresh viz_snap() without rendering the screen.
EXPORT(viz_snapshot) void viz_snapshot() { fillSnapshot(); }

// Select which per-mode LED engines run (bit m = mode m). A newly enabled engine starts from a reset state.
EXPORT(viz_modes) int viz_modes(int mask) {
  mask &= (1 << fv::LM_COUNT) - 1;
  for (int m = 0; m < fv::LM_COUNT; ++m)
    if ((mask >> m & 1) && !(g_modeMask >> m & 1u)) resetMode(m);
  g_modeMask = (uint32_t)mask;
  return mask;
}
EXPORT(viz_mode_leds) uint8_t* viz_mode_leds(int m) {  // wire RGB of engine m (viz_led_count() LEDs)
  return (uint8_t*)MODE_LED(m < 0 ? 0 : (m >= fv::LM_COUNT ? fv::LM_COUNT - 1 : m)).out();
}
EXPORT(viz_mode_stats) const int32_t* viz_mode_stats() { return g_modeStat; }  // [mode][mA, limiterQ8, activeMode, demandMa, budgetMa]
EXPORT(viz_mode_stat_n) int viz_mode_stat_n() { return MODE_STAT_N; }

EXPORT(viz_render) void viz_render(uint32_t tick) {
  g_tick = tick;
  const fv::Params& p = g_eng.params;
  fv::HudInfo hud;
  const bool themeLabel = g_hudTheme && tick < g_hudUntil;
  hud.label = themeLabel ? fv::theme_name((uint8_t)p.get(fv::P_THEME))
                         : fv::led_mode_name((uint8_t)p.get(fv::P_LED_MODE));
  hud.fps10 = g_fps10;
  hud.bpm = feat().bpm;
  const int32_t mode = p.get(fv::P_HUD);
  uint8_t show = 0;
  if (mode == 1) show = 255;
  else if (mode == 0 && tick < g_hudUntil) {
    uint32_t left = g_hudUntil - tick;
    show = left > 30 ? 255 : (uint8_t)(left * 255 / 30);  // son 0.5 s'de sön
  }
  hud.show = show;
  g_scr.beginFrame(feat(), p, hud, tick);
  for (int y = 0; y < fv::SCREEN_H; y += fv::STRIPE_ROWS)
    g_scr.renderRows(y, fv::STRIPE_ROWS, g_fb + y * fv::SCREEN_W);
  fillSnapshot();
}

EXPORT(viz_fb) uint16_t* viz_fb() { return g_fb; }
EXPORT(viz_leds) uint8_t* viz_leds(int pre) {
  return (uint8_t*)(pre ? g_eng.leds().pattern() : g_eng.leds().out());
}
EXPORT(viz_features) const fv::Features* viz_features() { return &g_eng.features(); }
EXPORT(viz_snap) const int32_t* viz_snap() { return g_snap; }
// Bant düzeni (fmin/fmax'a göre): bant i = FFT kutuları [edge(i), edge(i+1)); i = 0..NUM_BANDS. Hz = (kutu − 0.5)·FS/FFT_N.
EXPORT(viz_band_edge) int viz_band_edge(int i) { return feat().edge[i < 0 ? 0 : (i > fv::NUM_BANDS ? fv::NUM_BANDS : i)]; }
EXPORT(viz_snap_len) int viz_snap_len() { return SNAP_LEN; }

// --- parametreler ---
EXPORT(viz_set) int viz_set(int id, int v) {
  if (id < 0 || id >= fv::P_COUNT) return 0;
  const bool changed = g_eng.params.set((fv::ParamId)id, v);
  if (changed && id != fv::P_LED_MODE)
    for (int m = 0; m < fv::LM_COUNT; ++m) g_modeParams[m].v[id] = g_eng.params.v[id];
  if (changed && (id == fv::P_LED_MODE || id == fv::P_THEME)) {
    g_hudUntil = g_tick + 150;  // 2.5 s
    g_hudTheme = id == fv::P_THEME;
  }
  return g_eng.params.get((fv::ParamId)id);
}
EXPORT(viz_get) int viz_get(int id) {
  return (id < 0 || id >= fv::P_COUNT) ? 0 : g_eng.params.get((fv::ParamId)id);
}
EXPORT(viz_param_count) int viz_param_count() { return fv::P_COUNT; }
EXPORT(viz_param_name) const char* viz_param_name(int id) {
  return (id < 0 || id >= fv::P_COUNT) ? "" : fv::param_info((fv::ParamId)id).name;
}
EXPORT(viz_param_min) int viz_param_min(int id) {
  return (id < 0 || id >= fv::P_COUNT) ? 0 : fv::param_info((fv::ParamId)id).min;
}
EXPORT(viz_param_max) int viz_param_max(int id) {
  return (id < 0 || id >= fv::P_COUNT) ? 0 : fv::param_info((fv::ParamId)id).max;
}
EXPORT(viz_param_def) int viz_param_def(int id) {
  return (id < 0 || id >= fv::P_COUNT) ? 0 : fv::param_info((fv::ParamId)id).def;
}
EXPORT(viz_param_group) int viz_param_group(int id) {
  return (id < 0 || id >= fv::P_COUNT) ? 0 : fv::param_info((fv::ParamId)id).group;
}
EXPORT(viz_param_flags) int viz_param_flags(int id) {
  return (id < 0 || id >= fv::P_COUNT) ? 0 : fv::param_info((fv::ParamId)id).flags;
}
EXPORT(viz_param_desc) const char* viz_param_desc(int id) {
  return (id < 0 || id >= fv::P_COUNT) ? "" : fv::param_info((fv::ParamId)id).desc;
}
EXPORT(viz_param_group_count) int viz_param_group_count() { return fv::PG_COUNT; }
EXPORT(viz_param_group_name) const char* viz_param_group_name(int g) { return fv::param_group_name((uint8_t)g); }
EXPORT(viz_palette_count) int viz_palette_count() { return fv::LED_PAL_N; }
EXPORT(viz_palette_name) const char* viz_palette_name(int p) { return fv::led_palette_name((uint8_t)p); }
EXPORT(viz_led_count) int viz_led_count() { return g_eng.leds().count(); }
EXPORT(viz_led_max) int viz_led_max() { return fv::LED_MAX; }
EXPORT(viz_defaults) void viz_defaults() { g_eng.params.defaults(); syncModeParams(); }

// --- isimler ---
EXPORT(viz_mode_count) int viz_mode_count() { return fv::LM_COUNT; }
EXPORT(viz_theme_count) int viz_theme_count() { return fv::TH_COUNT; }
EXPORT(viz_scenario_count) int viz_scenario_count() { return fv::SC_COUNT; }
EXPORT(viz_mode_name) const char* viz_mode_name(int m) { return fv::led_mode_name((uint8_t)m); }
EXPORT(viz_theme_name) const char* viz_theme_name(int t) { return fv::theme_name((uint8_t)t); }
EXPORT(viz_scenario_name) const char* viz_scenario_name(int s) { return fv::scenario_name((uint8_t)s); }

// --- HUD ---
EXPORT(viz_hud) void viz_hud(int show) { g_eng.params.set(fv::P_HUD, show); }
EXPORT(viz_hud_flash) void viz_hud_flash() { g_hudUntil = g_tick + 150; g_hudTheme = 0; }
EXPORT(viz_set_fps) void viz_set_fps(int fps10) { g_fps10 = (uint16_t)(fps10 < 0 ? 0 : (fps10 > 9999 ? 9999 : fps10)); }

// --- tekil okuyucular ---
EXPORT(viz_level) int viz_level(int i) { return feat().level[clampBand(i)]; }
EXPORT(viz_bar) int viz_bar(int i) { return feat().bar[clampBand(i)]; }
EXPORT(viz_cap) int viz_cap(int i) { return feat().cap[clampBand(i)]; }
EXPORT(viz_led) int viz_led(int i) { return feat().led[clampBand(i)]; }
EXPORT(viz_raw) int viz_raw(int i) { return feat().raw[clampBand(i)]; }
EXPORT(viz_noise) int viz_noise(int i) { return feat().noise[clampBand(i)]; }
EXPORT(viz_beat) int viz_beat() { return feat().beat; }
EXPORT(viz_beat_env) int viz_beat_env() { return feat().beatEnv; }
EXPORT(viz_bpm) int viz_bpm() { return feat().bpm; }
EXPORT(viz_ceiling) int viz_ceiling() { return feat().ceiling; }
EXPORT(viz_silent) int viz_silent() { return feat().silent; }
EXPORT(viz_hue) int viz_hue() { return feat().centroidHue; }
EXPORT(viz_ma) int viz_ma() { return g_eng.leds().milliamps(); }
EXPORT(viz_demand) int viz_demand() { return g_eng.leds().demandMa(); }          // limiter öncesi model talebi (mA)
EXPORT(viz_budget) int viz_budget() { return g_eng.leds().budgetMa(); }          // son karede uygulanan etkin bütçe (mA)
EXPORT(viz_budget_now) int viz_budget_now() { return fv::led_budget_ma(g_eng.params); }   // şimdiki parametrelerle etkin bütçe
// Güç kaynağı USB mi? (adaptör algılama donanımının simülasyonu; ma_usb yalnız 1 iken etkili)
EXPORT(viz_set_usb) void viz_set_usb(int usb) {
  g_eng.params.usbPower = usb ? 1 : 0;
  for (int m = 0; m < fv::LM_COUNT; ++m) g_modeParams[m].usbPower = g_eng.params.usbPower;
}
EXPORT(viz_limiter) int viz_limiter() { return g_eng.leds().limiterQ8(); }
EXPORT(viz_active_mode) int viz_active_mode() { return g_eng.leds().activeMode(); }
EXPORT(viz_frame) uint32_t viz_frame() { return feat().frame; }

// --- golden ---
EXPORT(viz_scenario_hash) uint32_t viz_scenario_hash(int sc, int frames, int flags) {
  return fv::scenario_hash((fv::Scenario)sc, frames, 1, (uint32_t)flags);
}
// Mod ve LED sayısı seçili sürüm (golden "leds_param" paritesi); seed 1.
EXPORT(viz_scenario_hash_ex) uint32_t viz_scenario_hash_ex(int sc, int frames, int flags, int mode, int leds) {
  return fv::scenario_hash((fv::Scenario)sc, frames, 1, (uint32_t)flags, (uint8_t)mode, 0, 0, leds);
}
