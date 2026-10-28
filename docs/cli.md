# Serial CLI reference

The serial command line is the primary tuning interface of the ESP32 FFT visualizer: one line per command at
115200 baud. This page is the complete reference for the commands, the parameters they drive and the diagnostic
output they print. The IR remote key map and save flow are in [controls.md](controls.md).

## Connecting

- Settings: **115200 baud**, 8N1. A command runs when you press Enter; CR and LF both end a line.
- Opening the port can reset the board (most ESP32 boards toggle DTR/RTS). The boot banner ends with `READY`;
  wait about **4 s** after it before typing.
- Only one program may own the port: close the serial monitor before `tools/flash.sh` opens it.
- `Permission denied` on Linux means the user is not in the `dialout` group.
- The board does not echo typed characters (Backspace works). Command names are case-insensitive and a line
  holds up to 127 characters.
- Replies start with `OK`, `WARN`, `ERR` or `NOT`. The board also prints its own lines: button events, NVS
  write counts, and periodic output when `stats` or `stream` is running.
- A reset or reboot clears unsaved diagnostic state (`ledtest`, `ledoff`, `disp`, `bl`, `src`); saved settings
  remain.
- The firmware's replies and help texts are Turkish. Command and parameter names are written exactly as in
  this page.

## Conventions and side effects

Every command table uses these four effect labels:

| Label | Meaning |
|---|---|
| `read` | Read-only output; nothing changes. |
| `ram` | Changes running state only; a reset or reboot clears it. |
| `auto-save` | Changes a parameter; the firmware writes saved settings (NVS) 5 s after the last change. |
| `nvs` | Writes or erases stored data immediately. |

- `save` writes changed settings at once, `reset` deletes saved settings and presets (display settings
  included) and applies defaults, `reboot` restarts the board. None of the three is needed for ordinary use.
- `power reset` only clears the runtime peak counters; it is unrelated to `reset`.
- Values moved by `ramp`/`sweep` are temporary: the delayed save skips them and a reboot discards them.
  `save` writes the value that is current at that moment.

## Safe first session

Start with the read-only commands:

| Command | What you see |
|---|---|
| `help` | Grouped command summary; `help <command>` explains one command or parameter. |
| `params` | All parameters with value, range and default, grouped. |
| `get` | All current values as one JSON line. |
| `power` | Current model, effective budget and peak current estimates. |
| `stats` | One JSON status line. |
| `ir` | IR receiver state and key mapping. |

Do not use `save`, `reset`, `reboot` or `cal` casually: `save` writes stored settings immediately, `reset`
deletes them (presets included), `reboot` restarts the board, and `cal` relearns the noise floor and must run
in silence (see Calibration and autocal).

## Command reference

`[ ]` marks an optional argument and `|` an alternative. Without an argument a setting command reads its
value; with an argument it writes it.

### Help and parameter list

| Command / syntax | What it does | Effect |
|---|---|---|
| `help [command\|parameter]` (alias `?`) | Grouped command summary; with a name: usage, range, default, description and an example. Misspelled names get a nearest-name suggestion. | `read` |
| `params [group]` | Lists parameters by group (`genel serit ses ekran led renk desen gecis`); `*` marks values different from the default, `[anim]` marks parameters driven by `ramp`/`sweep`. | `read` |
| `get [param]` | Without an argument all parameters as one JSON line (plus `spi`, `rot`, `offx`, `offy`, for tooling); with a name a single value. | `read` |

### Setting values

| Command / syntax | What it does | Effect |
|---|---|---|
| `set <param> <value>` | Writes any parameter of the parameter reference. | `auto-save` |
| `<name> [value]` | Every parameter name is also a command (`bri 128`, `leds 60`, `pal okyanus`); without a value it prints the current one. | `auto-save` (`read` without a value) |

Value forms:

- Integers (decimal or `0x` hex). Out-of-range values are clamped to the parameter range and reported with a
  `WARN` line; an unparsable value gives `ERR <name>: ...` with the expected range.
- `auto` for `gain` selects automatic scaling (stored as `-128`).
- `off` (or `kapali`) sets any parameter whose minimum is 0 to `0`.
- Named values for `mode`, `theme` and `pal`, matched case-insensitively: `mode fire`, `theme neon`,
  `pal okyanus`. The names are listed in the parameter tables.
- Durations for `ramp`/`sweep`: a bare number is milliseconds, or use the suffixes `ms`, `s`, `m`
  (`3000`, `3s`, `500ms`, `2m`).
- Writing a parameter by hand stops a running `ramp`/`sweep` on it, and the new value then saves normally.

### Save, reset, reboot

| Command / syntax | What it does | Effect |
|---|---|---|
| `save` | Writes changed settings to NVS at once; values moved by `ramp`/`sweep` are saved as they are at that moment. | `nvs` |
| `reset` | Factory defaults: deletes all stored settings and presets (display settings included) and applies defaults; no reboot needed. | `nvs` |
| `reboot` | Restarts the board. | `ram` |

### Test and diagnostic commands

These are not ordinary settings: most are cleared by a reboot and none of them is needed for normal use.

| Command / syntax | What it does | Effect |
|---|---|---|
| `stats [hz]` | One JSON status line; `hz` > 0 prints periodically (max 20 Hz), `0` stops. | `read` |
| `power [reset]` | Current model, effective budget (which of `ma`/`psu`/`usb` sets the limit), current and peak mA, limiter share; `reset` clears the runtime peak counters only. | `read` (`power reset`: `ram`) |
| `stream [hz]` | Bar stream as `F` lines (for tooling); no argument = 30 Hz, max 60, `0` stops. | `read` |
| `ledtest [0-7\|auto]` | Fixed LED patterns: 1 red, 2 green, 3 blue, 4 white RGB(32,32,32), 5 markers (first LED red, last LED blue, every 10th white), 6 rainbow, 7 traveling dot. `auto` steps all patterns 6 s each; `0` (also the bare form) returns to normal output. | `ram` |
| `bands` | Per-band diagnostic: raw level and noise floor (dBFS × 10), level, bar, center Hz, band edges. | `read` |
| `raw [n]` | Raw ADC statistics over `n` samples at 48 kHz (default 4800, min 16, max 96000): mean, rms, min, max, clipping. | `read` |
| `rawdump [n]` | Raw ADC waveform as 4-digit hex after the statistics line; `n` <= 1024 (default 1024, min 16). | `read` |
| `drive [0-3]` | Display SCK/MOSI/DC drive strength (0 = 5 mA .. 3 = 40 mA); diagnostic experiment for edge/current noise. | `ram` |
| `disp [0\|1]` | Stops (0) / resumes (1) display drawing and SPI traffic; used for noise hunting. | `ram` |
| `bl [on\|0-255]` | Backlight: `on` = constant level without PWM (the normal state), a number = PWM brightness; bare form = PWM 200. | `ram` |
| `ledoff [0\|1]` | Forces the LED output black (1; also the bare form) / back to normal (0). | `ram` |
| `src [mic\|0..6]` | Input source: microphone or an internal synthetic scenario: 0 silence, 1 sine1k, 2 sweep, 3 kick120, 4 mix (music-like), 5 pink, 6 tone100. | `ram` |
| `inj <Hz> <dBFS>` | Adds a sine of known level to the microphone samples (0..20000 Hz, dBFS -90..0 relative to full scale); `inj 0` turns it off. | `ram` |
| `skip [0\|1]` | Skips unchanged display strips (bare form = 1); `0` always sends full frames, the runtime default. | `ram` |
| `selftest` | Installation self-test (display, LED, mic, sample rate, DSP, interference, input). | `ram` |
| `dsp` | Runs only the DSP golden hash test. | `read` |
| `cal` | Noise calibration: learns the noise floor from 2 s of sound. Keep the room quiet; never run it during music. | `ram` |
| `bench` | Performance measurement (JSON, µs). | `read` |
| `spi [10-80]` | Display SPI clock in MHz. | `auto-save` |
| `rot [0-3]` | Display rotation. | `auto-save` |
| `offs [x y\|auto]` | Display RAM offset `x y` in 0..80 (0/80 on 240x320 controllers); `auto` restores automatic detection. | `auto-save` |
| `ir` | IR receiver status; `ir learn on\|off` toggles code learning (see IR remote and learning). | `read` (`ir learn`: `ram`) |

With no argument `spi`, `rot` and `offs` print the current display settings (`-1` offset = automatic).

### Automation

| Command / syntax | What it does | Effect |
|---|---|---|
| `ramp <param> [start] <target> <duration>` | Linear move to the target over the duration; without a start value it begins at the current one. Runs on the board, clipped to the parameter range. `ramp stop [param]` stops ramps (all if no name). | `ram` |
| `sweep <param> <min> <max> <period> [tri\|sin]` | Continuously oscillates between min and max; `tri` (default) or `sin` shape. `sweep stop [param]` stops sweeps. | `ram` |
| `anim` | Lists running `ramp`/`sweep` jobs with current values; `anim stop [param]` stops them and values stay where they are. At most 4 animations run at once. | `read` (`anim stop`: `ram`) |
| `palcycle [seconds\|off]` | Shortcut for `pal_cyc`: palettes slide to the next one every period; `0`/`off` disables it. | `auto-save` |
| `preset save\|load\|del <0-3>` | Stores, loads or deletes a look in one of 4 slots (`del` also accepts `clear`); `preset list` shows the slots. Setup settings are never included: `leds`, `ma`, `fmin`, `fmax`, `gain`, `agcmin`, `hud`. | `nvs` (save/del), `auto-save` (load) |

`ramp`/`sweep` reject parameters flagged `no-ramp` below (selections and layout changes).

## Parameter reference

Values are plain integers unless noted. Many speeds and levels are **Q8** fixed point: the real value is the
written integer divided by 256.

| Written | Real | Meaning |
|---|---|---|
| `256` | 1.00 | 100 % |
| `154` | 154/256 = 0.60 | 60 % |
| `128` | 128/256 = 0.50 | 50 % |
| `50` | 50/256 = 0.20 | 20 % |
| `0` | 0 | 0 % |

For example `led_th 50` means threshold 0.20: a band below 20 % of the 0..1 scale lights no LED. The speed
settings (`*_atk`, `*_rel`) are Q8 fractions of the remaining distance toward the target per frame (60 frames
per second); a bigger number is faster, `atk` is rise, `rel` is fall. Other units are named per parameter:
Q16/frame^2 (`cap_grav`), frames at 60 per second (`cap_hold`, `fade`, `demo_fade`), seconds (`demo_sec`,
`pal_cyc`), Hz (`fmin`, `fmax`), dBFS (`gain`, `agcmin`), 0.1 dB/octave (`tilt`), mA and µA (power
model), percent (`psu_pct`), hue steps (`hue_ofs`, `hue_kick`), V (`ember`, `idle_bri`), per 256 frames
(`fire_spark`), LED per frame (`wf_spd`).

Flags: `setup` = installation/hardware setting, never stored in presets; `no-ramp` = cannot be driven by
`ramp`/`sweep`. Defaults are the factory values applied by `reset`
([core/src/fv_params.cpp](../core/src/fv_params.cpp)).

### General

| Name | Range | Default | Flags | Meaning |
|---|---|---|---|---|
| `mode` | 0-8 | 0 | no-ramp | LED mode: `0` AURORA, `1` SPARKLE, `2` MIRROR, `3` SPECTRUM, `4` BASS, `5` WATERFALL, `6` FIRE, `7` COMET, `8` DEMO. |
| `theme` | 0-4 | 0 | no-ramp | Screen theme: `0` SPECTRUM, `1` EMBER, `2` NEON, `3` FIRE, `4` CHROMA. |
| `bri` | 0-255 | 255 | - | LED brightness ceiling. |
| `hud` | 0-2 | 0 | setup, no-ramp | Screen info overlay: 0 automatic (shows 2.5 s after a change), 1 always, 2 off. |

### Strip and power

| Name | Range | Default | Flags | Meaning |
|---|---|---|---|---|
| `ma` | 100-20000 | 2500 | setup, no-ramp | LED current budget (mA, model estimate); the limiter never exceeds it. |
| `leds` | 1-300 | 120 | setup, no-ramp | Active LED count (2 m = 120, 1 m = 60, 3 m = 180). |
| `led_ma` | 5-100 | 60 | setup, no-ramp | Full-white current per LED (mA); 60 = WS2812B datasheet value - measure your strip or calculate it from the label. |
| `led_idle` | 0-5000 | 1000 | setup, no-ramp | Idle current per LED (µA); even a black LED draws it. |
| `psu_ma` | 0-30000 | 0 | setup, no-ramp | Power supply capacity (mA); 0 = not specified (only `ma` applies). |
| `psu_pct` | 20-100 | 80 | setup, no-ramp | Fraction of the supply to use (%); effective budget <= psu_ma·psu_pct/100. |
| `ma_usb` | 50-5000 | 500 | setup, no-ramp | Current budget on USB power (mA); only with adapter detection enabled. |

### Audio and analysis

| Name | Range | Default | Flags | Meaning |
|---|---|---|---|---|
| `gain` | -128..3 | -128 | setup, no-ramp | Fixed AGC ceiling (dBFS); -128 = automatic. |
| `agcmin` | -60..-10 | -45 | setup | Lower limit of the AGC ceiling (dBFS). |
| `tilt` | 0-60 | 15 | - | Spectrum tilt (0.1 dB/octave). |
| `beat_sens` | 128-1024 | 384 | - | Beat sensitivity (Q8 deviation multiplier; smaller = more sensitive). |
| `fmin` | 40-1500 | 250 | setup, no-ramp | Lowest displayed frequency (Hz); changing it requires `cal`. |
| `fmax` | 4500-10000 | 10000 | setup, no-ramp | Highest displayed frequency (Hz). |
| `autocal` | 0-1 | 0 | setup, no-ramp | Automatic noise calibration at boot (2 s window, first attempt 3 s after boot); rejects and retries when music, sound or noise is not steady. See Calibration and autocal. |

### Screen

| Name | Range | Default | Flags | Meaning |
|---|---|---|---|---|
| `bar_atk` | 1-255 | 154 | - | Screen bar rise speed (Q8). |
| `bar_rel` | 1-255 | 31 | - | Screen bar fall speed (Q8). |
| `cap_hold` | 0-120 | 18 | - | Peak marker hold time (frames). |
| `cap_grav` | 1-1024 | 66 | - | Peak marker fall acceleration (Q16/frame^2). |
| `bar_th` | 0-230 | 84 | - | Screen bar threshold (Q8); below it bars stay invisible. |

### LED response

| Name | Range | Default | Flags | Meaning |
|---|---|---|---|---|
| `led_atk` | 1-255 | 166 | - | LED band rise speed (Q8). |
| `led_rel` | 1-255 | 56 | - | LED band fall speed (Q8). |
| `led_th` | 0-230 | 50 | - | LED threshold (Q8); below it only embers. |
| `bloom` | 0-230 | 128 | - | LED glow spread (Q8 kernel). |
| `led_ae` | 0-255 | 48 | - | Auto-exposure target (0 = off). |
| `bass_atk` | 1-255 | 179 | - | Bass envelope rise speed (Q8; BASS/FIRE). |
| `bass_rel` | 1-255 | 26 | - | Bass envelope fall speed (Q8). |
| `dither` | 0-1 | 1 | - | LED quantization: 0 temporal (flickers at low levels), 1 fixed spatial (no flicker). |

### Color and palette

| Name | Range | Default | Flags | Meaning |
|---|---|---|---|---|
| `hue_spd` | -255..255 | 6 | - | Hue flow speed (Q8 hue/frame; 6 = about 1.4 hue/s; negative = reverse direction). |
| `hue_ofs` | 0-255 | 0 | - | Fixed hue shift (0..255 = full color circle). |
| `hue_kick` | 0-30 | 6 | - | Hue kick on beat (hue). |
| `pal` | 0-5 | 0 | - | LED palette: `0` RAINBOW, `1` WARM, `2` OCEAN, `3` FOREST, `4` PURPLE, `5` GOLD. The CLI accepts the firmware spellings `GOKKUSAGI`, `SICAK`, `OKYANUS`, `ORMAN`, `MOR`, `ALTIN` (any case). |
| `pal_cyc` | 0-255 | 0 | - | Palettes slide in rotation: period in seconds; 0 = off. |
| `smooth` | 0-1 | 1 | - | Color transitions: 1 smooth (no palette seam, hue kick spread out, center hue filtered), 0 old behavior (jumpy). |

### Patterns

| Name | Range | Default | Flags | Meaning |
|---|---|---|---|---|
| `wf_spd` | 0-512 | 192 | - | WATERFALL scroll speed (Q8 LED/frame; 192 = 0.75). |
| `wf_dec` | 128-255 | 247 | - | WATERFALL trail persistence (Q8; large = long trail). |
| `fire_cool` | 0-200 | 75 | - | FIRE cooling base (large = short flame). |
| `fire_spark` | 0-200 | 40 | - | FIRE spark rate base (per 256 frames). |
| `comet_spd` | 32-1024 | 256 | - | COMET speed multiplier (Q8; 256 = 1×). |
| `comet_trl` | 100-250 | 204 | - | COMET trail persistence (Q8; large = long tail). |
| `ember` | 0-60 | 9 | - | Ember brightness: base V of LEDs below the threshold. |

### Transitions and idle

| Name | Range | Default | Flags | Meaning |
|---|---|---|---|---|
| `idle_bri` | 0-150 | 22 | - | Rainbow base brightness in silence (V). |
| `idle_spd` | 0-1024 | 128 | - | Idle animation speed (Q8 phase/frame). |
| `fade` | 1-120 | 24 | - | Manual mode-change transition time (frames, 60/s). |
| `demo_beats` | 1-255 | 32 | - | DEMO: beats per sub-mode. |
| `demo_sec` | 1-120 | 30 | - | DEMO: maximum time per sub-mode without beats (s). |
| `demo_fade` | 1-120 | 30 | - | DEMO sub-mode transition time (frames). |

## Calibration and autocal

- `cal` learns the noise floor from **2 s** of sound. Keep the room quiet and never run it during music: the
  capture interval is trusted as it is. Manual calibration is always available, takes priority over the
  automatic attempts and clears the `KALIBRASYON GEREKLI` hint. Changing `fmin`/`fmax` restarts the band
  layout and the learned noise floors, so calibrate again in silence afterwards.
- `autocal` enables the automatic noise capture at boot. Registry default `0`; firmware builds default
  `CFG_AUTOCAL_BOOT=1` ([firmware/fftviz/config.h](../firmware/fftviz/config.h)). Existing saved values win
  over defaults: changing a default does not migrate saved choices.
- Timing: the first attempt opens about **3 s** after boot (or after a band restart). Acceptance needs **two
  consecutive 2 s windows** that pass all criteria (around the 7th second). A rejected attempt retries after
  **5 s**, at most **six rejections** (about 42 s), then autocal gives up and the screen keeps the
  `KALIBRASYON GEREKLI` hint until a manual `cal`.
- Rejection criteria in one 2 s window: level (display bands total above -15.0 dBFS or the bass channel above
  -16.0 dBFS; the board's own quiet noise is about -22 dBFS, with `drive 3` about -17), steadiness (band
  groups or the bass channel move more than 1.5 dB inside the window), peakiness (one band rises more than
  3.3 dB above its neighbours).
- Every event prints one serial line (`AUTOCAL ...`): start (attempt n/6), window passed, accept
  (`AUTOCAL OK`), reject with the measured value against the limit, retry, give up. Enabling `autocal` late
  does not impose a fresh 3 s delay. Turning it off stops automatic attempts only: manual `cal`, startup fast
  learning, continuous noise tracking, audio AGC and LED auto-exposure keep working.

## Power model

The board does not measure current. Each frame estimates the LED current with a model and dims the strip at
once when the estimate would exceed the effective budget. This is model-based protection: how well `led_ma`
and friends match your hardware decides how accurate it is.

```
current (mA) = LED count × led_idle / 1000 + (sum of R+G+B of all LEDs) × led_ma / 765
```

At full white (R=G=B=255) one LED draws `led_ma` mA (765 = 3 channels × 255).

```
effective = min( ma,  psu_ma × psu_pct / 100  [when psu_ma > 0],  ma_usb  [when USB is detected] )
```

The parameters: `ma` is the budget you want for the strip; `psu_ma` is the supply capacity (0 = not
specified), `psu_pct` the share of it to use (default 80); `ma_usb` applies on USB power only with adapter
detection enabled (`CFG_ADP_DETECT=1` and the board wiring); `led_ma` is the full-white current per LED
(60 = WS2812B datasheet ceiling; a value below the real current weakens protection) and `led_idle` the idle
current per LED (µA).

`power` prints the model line, the budget line (which limit binds: `ma`, `psu` or `usb`) and the runtime
line: current estimate, demand before the limiter, limiter factor (255 = no limiting), peaks since boot or
`power reset`, the share of frames over budget in the last second and the `AKIM SINIRI` warning count. When
demand exceeds the budget for more than half of the last second in two consecutive windows the screen shows
the `AKIM SINIRI AKTIF` warning; single beat peaks do not trigger it. Lower `bri` or `led_ae`, or correct the
model and budget for your hardware.

The firmware warns immediately about inconsistent settings: effective budget at or below the strip idle
current (strip stays completely black), `psu_pct` > 90 (heat and voltage drop; leave about 0.2 A headroom if
the ESP32/display share the supply), `ma_usb` > 500 (USB 2.0 ports usually give 500 mA), `ma_usb` with
detection disabled (no effect), `led_ma` < 60 (protection loosens), `ma` > 5000 (check strip and wire gauge,
add feed points).

The `ledtest` patterns are part of the same model: `ledtest 4` is white RGB(32,32,32) regardless of `bri`,
which is about **1023 mA** of model demand at 120 LEDs / 60 mA / 1000 µA idle - not a guarantee of
"below 1 A". All patterns use low color values, run at the active `leds` length and are clamped to the `ma`
budget; `ledtest 0` returns to normal output. All figures are model estimates; verify real current with a
multimeter or USB power meter.

## Diagnostic outputs

### stats

One JSON line. Field names are as printed:

| Field | Meaning |
|---|---|
| `fs` | Actual ADC sample rate (Hz); healthy about 48000 (±2 %). |
| `afps`, `dfps` | Audio analysis and display frame rate (about 60 and 45). |
| `dsp_us`, `dsp_max_us`, `dsp_alarm` | Audio processing time, last 1 s average and longest (µs; about 1500, under 2000); `dsp_alarm` = 1 when `dsp_max_us` exceeds 2000. |
| `render_us`, `render_max_us`, `frame_us` | Display draw time (average/longest) and the full display loop including DMA wait (µs). |
| `stripes_sent`, `stripes_skipped` | Display strips sent / skipped because unchanged (total counters). |
| `leds`, `ma` | Active LED count; LED current estimate after the limiter (mA). |
| `limiter` | Limiter factor: 255 = no limiting, lower = strip dimmed. |
| `budget`, `demand` | Effective current budget / demand before the limiter (mA). |
| `ma_peak`, `lim_pct`, `lim_frames` | Peak estimate since boot or `power reset`; share (%) and count of frames over budget in the last 1 s. |
| `src` | Power source: `off` (detection disabled), `adp` (adapter), `usb`. |
| `mode`, `amode` | Requested LED mode / actually running mode (they differ in DEMO). |
| `theme`, `bri`, `gain` | Current values (`gain` -128 = auto). |
| `bfp` | Internal FFT input scaling shift (diagnostic). |
| `ceiling_db` | Current auto-scale ceiling (dBFS); never below `agcmin`. |
| `beats`, `bpm` | Total detected beats and tempo estimate. |
| `silent` | 1 = the environment counts as silent (about 4 s of silence). |
| `clip`, `clip1s` | Mic samples touching 0 or 4095: total since reset (rising = mic gain too high) and in the last 1 s. |
| `dc_raw` | Microphone DC level (ADC units 0-4095); about 2048 (1.65 V). |
| `adc_ovf`, `adc_bad`, `adc_to` | ADC buffer overflows (samples lost), bad/wrong-channel reads, read timeouts. Healthy = 0. |
| `rmt_ok`, `rmt_busy`, `rmt_fail` | LED frames sent, skipped because the previous send was still running, failed. |
| `spi_err`, `spi_mhz` | Display SPI error count (0) and SPI clock (MHz). |
| `heap`, `heap_min` | Free heap and lowest free heap since boot (bytes). |
| `stack_free` | Free stack of the three tasks (audio, display, main loop); must not approach 0. |
| `up`, `snap` | Uptime in seconds; 1 = status snapshot read succeeded. |

Quick read: `fs` near 48000 and `adc_ovf`, `adc_bad`, `adc_to`, `rmt_fail`, `spi_err`, `clip` all zero, with
`limiter` at 255.

### bands

One JSON line with one entry per band (40 bands, low to high): `ceil` scale ceiling (dBFS × 10; -180 =
-18.0 dBFS), `bfp` as in stats, `raw` raw level per band (dBFS × 10; -9990 = too low to measure), `noise`
learned noise floor per band (dBFS × 10), `level` level after noise removal and scaling (0-255), `bar` screen
bar height (0-255), `hz` center frequency, `edge` 41 band edges in FFT bins (bin = 23.4375 Hz; band b is
`[edge[b], edge[b+1])`), `bass` and `bassN` the bass channel (47-150 Hz, beats and the LED bass region) raw
level and floor (dBFS × 10), independent of `fmin`. In silence `bar` and `level` should sit near 0; a band
stuck high means fixed interference at that frequency, and a high `noise` entry asks for `cal`.

### stream

Lines of the form `F <frame> <bfp> <beat> <mA> <80 hex chars>`. The 80 characters are the 40 bar heights,
two hex digits each (`00`-`ff`). This is for tooling; `bands` is easier to read by hand. `stream 0` stops.

### selftest

Prints `TEST <name> PASS|FAIL|SKIP <detail>` per step and a final `SELFTEST PASS|FAIL n/m`. Steps: DISPLAY,
LED, MIC, FS, DSP, INTERFERENCE, then INPUT. The interactive BOOT entry prompts for a fresh OK press within
5 s; the CLI (non-interactive) marks INPUT as explicit SKIP and excludes it from the summary denominator.
While software standby is active `selftest` returns an error and runs no steps. DSP compares unchanged golden
hashes; MIC reports bias, min, max, clipping and analog gain/wiring advice.

### dsp

Runs only the DSP step and proves the computed results match the PC reference exactly. A FAIL means the
software computation is wrong, not the hardware.

### bench

One JSON line in µs: `engine_us` (whole pipeline per frame), `render_frame_us` (display drawing),
`rfft1024_us` (FFT), `analyze_us` (analysis), `leds_us` (per LED mode), `ws_encode_us` (LED send encoding),
`cpu_mhz`. Only needed for software work; `stats` is enough to follow a running problem.

## Tuning recipes

Note the current values with `get` before experimenting, and `save` when you are happy.

1. **LEDs too dim.** Check `get`: `bri` (255), `led_ae` (48), `led_th` (50); a low saved value may remain,
   `reset` restores defaults. `ledtest 4` is only low RGB32 white, not a brightness/capacity test by itself -
   check power path, model/limiter and common GND together, then `ledtest 0`. For quiet music, in order:
   `set led_ae 80`, `set led_th 35`, `bri 255`. If `power` shows demand over budget often (or `stats`
   `limiter` below 255), fit `led_ma` to the strip first, then raise `ma` within `psu_ma`.
2. **LEDs flicker.** If the strip still flickers under `ledtest 5` (a static pattern), it is not a software
   setting: check common GND, the data line and the level shifter. Fluorescent-like shimmer at very dim
   levels: `get dither` should be `1`; write `dither 1`. Music-related flicker: `set led_rel 30`,
   `set led_atk 120`, `set led_th 80`, `set led_ae 32`, `set bloom 160`; SPARKLE is intentionally sparkly -
   try `mode 0` or `mode 3`.
3. **Colors jump or stutter.** `get smooth` should be `1`; write `smooth 1`. Remaining beat flashes (white
   flash in BASS/WATERFALL, hot core in MIRROR/AURORA) are intentional; calm them with `hue_kick 0`.
   Brightness jumps can come from the limiter (`power`, `stats` `lim_pct`): fix `ma`/`led_ma` as in the
   power model.
4. **Bars move in silence.** After the final MAX4466 analog gain adjustment, stop the music and run `cal`
   (or hold HASH for 3 s) - never during music. If the screen shows `KALIBRASYON GEREKLI`, boot autocal was
   rejected. Still moving: `agcmin -35`, then `agcmin -30`; `set bar_th 110`. `bands` shows which band moves;
   a persistently high `noise`/`raw` is hardware - see recipe 7.
5. **Treble too strong or bass too weak.** Treble dominant: `set tilt 5` (flat response `set tilt 0`; default
   is 15; if too dull try 8-10). Weak bass: `set tilt 0` (bass gains up to +6.7 dB against the default,
   treble drops up to 3.8 dB; `tilt` cannot go below 0). For beat pulses use `mode 4` or `mode 5` with
   `set beat_sens 256`; check the mic gain pot and the distance to the speaker, and the `TEST MIC` advice of
   `selftest`.
6. **Empty left side / frequency range.** The default range is 250 Hz - 10 kHz: below 250 Hz the display SPI
   noise floor is 6 dB higher and test speakers play no bass, so those bands stay empty. With a bass-capable
   speaker: `set fmin 46` (the old range), then `cal`. Bass beats and the LED bass region work at any `fmin`.
   No bars at the top end: try `inj 8000 -24` - if a bar lights, the firmware is fine and the problem is the
   source/mic chain, distance or gain.
7. **Display noise on the audio.** `disp 0` stops drawing: if bars and LEDs calm down, the display traffic is
   the source; `disp 1` resumes. `bl on` keeps the backlight constant (the normal state); a PWM value like
   `bl 100` adds a 5 kHz tone, revert with `bl on`. `spi 20` may help (unmeasured, experimental). Hardware
   fix: RC filter on the mic VCC (100 Ω + 100 µF + 47 nF), separate GND, keep the mic cable away from the
   SCK/SDA lines; then `cal`.
8. **Test the LED data line.** `ledtest 1`, `ledtest 2`, `ledtest 3` must show red, green, blue in that
   order; anything else means color order or level shifter trouble. `ledtest 5`: first LED red, last LED
   blue, every 10th white; missing, shifted or random LEDs mean data line or common GND trouble. `ledtest 6`
   is the rainbow, `ledtest 7` the traveling dot. `ledtest auto` steps all patterns 6 s each and returns to
   normal; `ledtest 0` always returns. The patterns use low color values: `ledtest 4` is about 1023 mA of
   model demand, not a "below 1 A" guarantee; `ledoff 1` sends black.
9. **Save, restore, factory reset.** Every changed setting is stored automatically 5 s later; `save` writes
   immediately. `reset` deletes saved settings (presets and display settings included) and applies defaults
   at once. To restore a single setting: `set <name> <default>` with the default from the parameter tables.
10. **Response too slow or too fast.** Too slow: `set led_atk 220`, `set led_rel 90`, `set bar_atk 200`,
    `set bar_rel 60`, `set beat_sens 256`; if a delay remains, check `stats`: `afps` about 60 and `dfps`
    about 45 (otherwise the cause is load or hardware, not settings). Too fast: `set led_atk 100`,
    `set led_rel 28`, `set bar_atk 100`, `set bar_rel 16`, `set beat_sens 512`, `set cap_grav 30`,
    `set cap_hold 40`.
11. **Strip length 1 m / 3 m and strip current.** Length: on a 60 LED/m strip use `leds 60` for 1 m and
    `leds 180` for 3 m (`leds 300` is the maximum, 5 m at 60 LED/m; other densities: your own count).
    Verify with `ledtest 5`: the last LED must be blue. At 3 m and above, far LEDs turning yellow/red at full
    brightness mean voltage drop - feed 5 V and GND at the far end too. `ma` (default 2500) must stay within
    the supply (set `psu_ma`); a longer strip shares the same current, so average brightness drops. Current:
    never lower `led_ma=60` without measurement - a lower value underestimates real current and weakens
    protection. `ledtest 4` is RGB(32,32,32), independent of `bri` (about 1023 mA model demand at 120
    LEDs). Measure in a safe power path on a separate strip branch: `ledoff 1` gives I_black, RGB32 gives
    I_white32 (the PSU total also includes the U1 branch; `power` is a model, not a sensor). Then
    `led_idle = 1000 × I_black_mA / 120` and `led_ma = (I_white32_mA - I_black_mA) × 255 / (120 × 32)`,
    both calculated from a low level: do not loosen the limiter without validation at higher real load.
12. **PSU amperage and build defaults (user_config.h).** The firmware example is `psu_ma=3450`,
    `psu_pct=80`, `ma=2500` (3450 = 3800 - 350 mA, calculated, not a measured capacity). A 3800 mA supply
    with 600 mA reserved for U1/peripherals gives `psu_ma 3200` (= 3800 - 600): 80 % → 2560, effective
    2500 mA; total 3100 mA with 700 mA label margin (calculated). A 20 mA per pixel figure is [unverified];
    120×60 = 7.2 A RGB and the idle-inclusive model reaches 7.32 A of unlimited demand. The limiter is not
    fault protection. Build-time values: never edit
    [firmware/fftviz/config.h](../firmware/fftviz/config.h) - copy
    [firmware/fftviz/user_config.example.h](../firmware/fftviz/user_config.example.h) to
    `firmware/fftviz/user_config.h`, keep only what you change, and build with
    [tools/build_fw.sh](../tools/build_fw.sh) (compiles only, never flashes):

    ```bash
    cp firmware/fftviz/user_config.example.h firmware/fftviz/user_config.h   # not tracked by git
    # in user_config.h keep only what you want to change, e.g.:
    #   #define CFG_LED_MA   60
    #   #define CFG_PSU_MA   3450
    nice -n 19 tools/build_fw.sh --jobs 4   # compiles only; does not flash
    ```

    Priority: config.h default < NVS saved < CLI.

## IR remote and learning

- `ir` prints the receiver state (`READY`, `DISABLED` or `ERROR`), the learn state, the NEC address and
  per-key mapping (normalized LSB-first bytes) and the dropped/truncated counters. A wrong argument gives
  `ERR ir: ir | ir learn on|off` and changes nothing.
- `ir learn on` shows raw frames for diagnosis (it performs no key action) as normalized LSB-first bytes
  (`a=` address, `c=` command). Do not copy old reversed-byte 32-bit codes. Learning keeps one pending raw
  capture, accepts at most 5 records per second and drops extras under load; it never writes NVS or settings.
- Copy the learned bytes into `firmware/fftviz/user_config.h` as `CFG_IR_ADDRESS` and `CFG_IR_KEY_<label>`,
  rebuild and flash over USB (flashing needs explicit user approval). There is no auto-mapping and no NVS IR
  table.
- Entering or leaving learning clears press history and cancels an active save flow without writing slots;
  `ir learn off` ends diagnosis with cleared input history, and a fresh valid frame is required afterwards.
- The key map, holds, the confirmed save flow and the sensitivity key are described in
  [controls.md](controls.md).
