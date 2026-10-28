# Controls and serial reference

Behaviour of the IR remote and the serial CLI. There is one Arduino application: no WiFi, OTA, factory/control app or signed package (the page in this repository is a software preview and never talks to the board). The behaviour below is taken from the firmware source and the host tests; it is not a record of physical acceptance. Installing firmware, opening the serial port and anything that writes NVS change the board and are never required to compile.

## Remote control

NEC address `0x00`; command bytes below are hexadecimal and are the defaults for the author's 17-key handset. Sixteen of the seventeen codes were captured from it; key 2 (`0x46`) is not yet captured. Another handset may use other codes: read them with `ir learn on` and override them in `user_config.h` ([CLI reference](cli.md#ir-remote-and-learning)). Holds, saving, standby, the sensitivity key and power-loss behaviour are covered by host tests, not by physical acceptance.

| Key | Command | Normal action |
|---|---|---|
| 1 | 45 | Recall mode-only slot 1; an empty slot reports empty |
| 2 | 46 | Recall slot 2; physical capture outstanding |
| 3 | 47 | Recall slot 3 |
| 4 | 44 | Recall slot 4 |
| 5 | 40 | Recall slot 5 |
| 6 | 43 | Recall slot 6 |
| 7 | 07 | Short: none; hold at least 2 s: persisted software standby / wake |
| 8 | 15 | None, including holds |
| 9 | 09 | Press: sensitivity step Normal -45, High -52, Very high -60 dBFS, wrapping; HUD shows `HASSASIYET: NORMAL`, `HASSASIYET: YUKSEK` or `HASSASIYET: COK YUKSEK`; a hold steps once |
| 0 | 19 | Short release: HUD; hold at least 2 s: display power only |
| LEFT | 08 | Previous LED mode, wrapping |
| RIGHT | 5A | Next LED mode, wrapping |
| UP | 18 | Brightness +16, clamped to 255; hold repeat after 350 ms, then at least 110 ms apart |
| DOWN | 52 | Brightness -16, clamped to 0; same repeat policy |
| OK | 1C | Next theme; fresh confirmation in save/self-test contexts |
| STAR | 16 | Two fresh presses within 500 ms open slot-save; STAR cancels the flow |
| HASH | 0D | Hold at least 3 s: request one 2 s quiet calibration; short: none |

Save a mode: double STAR, fresh OK, fresh digit 1-6, fresh final OK. The final confirmation authorizes the slot write; an occupied slot requires overwrite confirmation. STAR or 10 s inactivity cancels. Six remote mode slots are separate from the CLI's four full presets.

Sensitivity key 9 moves the existing `agcmin` parameter (AGC floor) through the same setter as the CLI: a running `ramp`/`sweep` on it stops, and the value is written by the ordinary delayed NVS save 5 s after the last change. An `agcmin` set from the CLI outside the three levels moves to the next more sensitive level on the next press (-50 to -52); -60 wraps to -45. Standby and the save flow ignore the key. On the author's board `agcmin -60` raised quiet-music bars about 6.7x; it can also leave bars decaying for about 20 s after a loud passage or a level change. No new DSP model was added.

Display-off keeps audio and LEDs running. Software standby stops the visualization pipeline; a new held 7 or fresh BOOT wakes it. Active BOOT: short HUD; release after at least 2 s starts the interactive self-test. BOOT is not the current manual-calibration gesture.

## Serial CLI: USB only

Serial: 115200 baud, 8N1, CR or LF termination. Opening the port may reset the device; only one program may own the port. The table lists what each command family does; it is not a script to run.

| Purpose | Commands / interface | Side effects |
|---|---|---|
| Help and current values | `help`, `help autocal`, `params`, `params ses`, `get`, `get autocal`, bare `autocal` | Read values/help; port opening can still reset hardware |
| Parameter change | `set <name> <value>` or `<name> <value>` | Changed ordinary settings auto-save to NVS 5 s after the last change |
| Optional boot calibration | `autocal 0`, `autocal 1` | Changes an existing parameter and can auto-save; does not disable continuous tracking/AGC/LED exposure |
| Manual quiet calibration | `cal` or held HASH 3 s | Captures 2 s of current sound as noise; keep the room quiet |
| Audio observation | `bands`, `raw`, `rawdump`, `stats`, `stream` | Diagnostic output; use command-specific help for rates/modes |
| Audio stimulus / diagnostics | `src`, `inj`, `bench`, `selftest`, `dsp`, `ir` | Can replace input, start tests or alter diagnostic state; not passive observation |
| Visual settings | Registry parameters including `mode`, `theme`, `bri`, `leds`, palette and speed parameters | Ordinary changes may auto-save |
| Diagnostic LED patterns | `ledtest`, `ledoff`, `power` | Not the nine music-reactive mode previews; limits are a model, not measured current |
| Automation / four CLI presets | `ramp`, `sweep`, `anim`, `palcycle`, `preset` | Changes settings; preset writes/deletes are persistent |
| Display interface | `spi`, `rot`, `offs`, `skip`, `drive`, `disp`, `bl` | State/configuration changes; some configuration persists |
| Explicit persistence / destructive commands | `save`, `reset`, `reboot` | `save` writes; `reset` clears stored settings/presets; `reboot` restarts. Never use as a diagnostic shortcut |

Use `help <command>` before commands with arguments; this inventory does not invent argument syntax. `power reset` clears runtime peak counters, unlike the destructive global `reset`.

## Calibration facts

- Registry default `autocal=0`, firmware default `CFG_AUTOCAL_BOOT=1`. Existing NVS wins over defaults; changing a default does not migrate saved choices.
- Boot autocal first becomes eligible around 3 s after startup. Acceptance requires two 2 s windows; rejected attempts retry after 5 s, up to six rejections. Enabling late does not impose a fresh 3 s delay.
- Turning autocal off stops automatic attempts, not explicit calibration, startup fast learning, continuous noise tracking, audio AGC or LED AE.
- Manual calibration trusts the capture interval; do not trigger it during music.

## Flash layout and saved settings

Standard single-app map (the core's "No OTA" scheme): `nvs` 0x9000 size 0x5000, `otadata` 0xe000 size 0x2000, `app0` 0x10000 size 0x200000, `spiffs` 0x210000 size 0x1e0000 (unused), `coredump` 0x3f0000 size 0x10000. `tools/build_fw.sh` fails if nvs moves, a second app slot appears or the image does not fit. Saved settings live in nvs; a firmware install writes only the bootloader (0x1000), partition table (0x8000), otadata (0xe000) and app (0x10000) and never writes or erases 0x9000-0xDFFF. The padded `*.merged.bin` image spans nvs and is deleted by the build.

## Evidence

- Source: `firmware/fftviz/{ir_control.cpp,ir_control.h,controls.cpp,audio_task.cpp,cli.cpp,config.h}`.
- Behavior tests: `tests/test_ir_control.cpp` (key map, holds, sensitivity cycle), `tests/test_task_lifecycle.cpp` and `tests/test_standby_diagnostics.cpp` (ADC lifecycle), `tests/test_quiet_recovery.cpp` (quiet music).
- Full command and parameter reference: [cli.md](cli.md).
