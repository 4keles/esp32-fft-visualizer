# Hardware

This document describes the one fully fitted design ('B') of the ESP32 audio visualizer: a MAX4466
microphone module, an ST7789 display and a WS2812B LED strip, every listed part populated, no
alternative or bypass feed options. The netlist at
[hardware/mimari-v2/netlist.json](hardware/mimari-v2/netlist.json) is the single electrical source;
part values, nets and pin numbers here follow it. There is no Wi-Fi and no OTA: firmware is
programmed over USB only, and nothing in the repository's software tests proves assembly or
electrical acceptance.

## Overview

- Sound path: MIC1 (MAX4466) -> R6 1 kΩ / C3 10 nF low-pass (approximately 15.9 kHz) -> ESP32 ADC1
  on GPIO34 -> analyzer (FFT) on the ESP32.
- Display path: ST7789 (DSP1) over SPI, with R3/R4/R5 47 Ω series resistors fitted close to the
  ESP32: SCK GPIO18, MOSI GPIO23, DC GPIO21, reset GPIO22, backlight GPIO19 (fixed HIGH, no PWM).
- LED path: GPIO25 -> U2 SN74HC00N (two NAND stages in series as the level buffer) -> R1 220 Ω ->
  strip DIN. R2 10 kΩ pulls DATA low at boot so the strip does not flicker during upload.
- IR receiver OUT on GPIO16 (module VCC from V33_DSP, return DGND).
- Power chain: PSU_P -> F1 -> FUSED -> SW1 -> V5, then D2 to ESP5V for U1, D4/D5 drops to U2 VCC,
  and 3V3 filter branches for display/IR and microphone. See Power architecture.

## Drawings and files

Four files under `docs/hardware/mimari-v2/` are kept with this repository:

| File | Content |
|---|---|
| [netlist.json](hardware/mimari-v2/netlist.json) | Single electrical source: parts, values, nets, class fields |
| [mimari-b.svg](hardware/mimari-v2/mimari-b.svg) | Full schematic of the whole B circuit |
| [plaket-ust.svg](hardware/mimari-v2/plaket-ust.svg) | Perfboard top view, part side |
| [plaket-bakir.svg](hardware/mimari-v2/plaket-bakir.svg) | Mirrored underside / wire view |

Glossary of the Turkish names and labels used in the drawings and the netlist:

- mimari = architecture
- plaket = perfboard
- ust = top
- bakir = copper (underside)
- netlist keys: pinler = pins, deger = value, sinif = class

Drawing labels and netlist prose are Turkish; part references such as U1 and R6 and part values are
language-independent. The perfboard geometry 110 x 65 mm with 40 x 25 holes is provisional
[unverified]. The drawings are assembly drawings, not 1:1 drilling templates.

## Pin map

Authoritative GPIO numbers and defaults are `CFG_PIN_*` in
[firmware/fftviz/config.h](../firmware/fftviz/config.h), with compile-time guards in
[firmware/fftviz/pins.h](../firmware/fftviz/pins.h).

| Function | GPIO | Macro | Net / connection |
|---|---:|---|---|
| TFT clock | 18 | `CFG_PIN_TFT_SCK` | SCK_E -> R3 47 Ω -> SCK |
| TFT data | 23 | `CFG_PIN_TFT_MOSI` | SDA_E -> R4 47 Ω -> SDA |
| TFT DC | 21 | `CFG_PIN_TFT_DC` | DC_E -> R5 47 Ω -> DC |
| TFT reset | 22 | `CFG_PIN_TFT_RST` | RES |
| TFT backlight | 19 | `CFG_PIN_TFT_BLK` | BLK, fixed HIGH, no PWM |
| LED data | 25 | `CFG_PIN_LED` | DATA -> U2 two NAND stages -> R1 220 Ω -> DIN |
| Microphone | 34 | `CFG_PIN_MIC` | ADC1_CH6, after R6 1 kΩ / C3 10 nF |
| IR OUT | 16 | `CFG_PIN_IR` | IR_OUT; VCC V33_DSP, return DGND |
| On-board BOOT | 0 | `CFG_PIN_BTN_BOOT` | The board's own button; no new external wire |

- GPIO4 stays externally not connected (NC): the firmware configures it INPUT_PULLDOWN only (no
  read, interrupt, output or PWM), and there is no SW2 in this design.
- Forbidden for new wiring: flash pins 6-11, strapping pins 0, 2, 5, 12, 15, and UART0. Unused
  GPIOs stay free. The microphone stays on an ADC1 pin (GPIO34).
- If the module is WROVER/PSRAM, GPIO16 cannot be used for the IR receiver; no other pin is
  guessed, the build stops there.
- GND connection points: U1.GND_T5 is the end of the on-board GND chain (single 24 AWG feed from
  the J1.4 pin tail, C7.- local return) and the strip current never passes through it; U1.GND_B12
  is DGND only (display/IR/C6); U1.GND_B18 is AGND only (microphone/C3/C4/C5).

## Power architecture

- Net chain: PSU_P -> F1 -> FUSED -> SW1 (two identified real contacts) -> V5. Pulling F1 opens
  PSU_P-FUSED; SW1 OFF opens FUSED-V5. There is no bypass. FUSED, V5 and ESP5V are never merged.
- D1 1N4007 crowbar: D1.K on FUSED, D1.A on power GND. Wired in reverse it blows F1; never fit D1
  without F1 (it would short the supply).
- D2 1N5819: A on V5 (after F1/SW1), banded K on ESP5V -> C7.+ -> U1.5V. D2 blocks the USB ->
  strip path; ESP5V is not joined to V5.
- D4/D5 are two separate 1N4007 drops feeding U2 VCC: D4.A on V5, D4.K -> D5.A, D5.K -> U2.14
  (with C2.1). U2 is never fed from 5 V directly.
- C7 100 µF/16 V sits near D2/U1 with the local return ESP5V -> U1.GND_T5. C7 does not cure low
  voltage headroom or replace missing on-board bypass.
- 3V3 filter branches hang off U1.3V3: FB1.1, then FB1.2 -> V33_DSP -> DSP1.VCC / C6.+ / IR1.VCC;
  and R7.1, then R7.2 -> V33_MIC -> MIC1.VCC / C4.+ / C5.1. The U1 supply is not downstream of
  these branches.
- GND star is the J1.3 pin: PSU COM enters the J1.3 plug screw, the J1.3 and J1.4 pin tails are
  bridged with 1.5 mm² wire, and the strip return J2.1 lands on the J1.3 pin tail directly without
  passing through U1. A single 24 AWG feed leaves the J1.4 pin tail into the board.
- On the perfboard GND is a chain, not a star per part: D1.A -> U2 GND pins (7/9/10/12/13) ->
  C2.2 -> R2.2 -> C7.- -> U1.GND_T5 (about 140 mm 24 AWG, about 7 mV at 0.6 A
  [calculation: 84 mΩ/m]).
- The 1.5 mm² external harness (PSU -> J1 -> F1 -> SW1 -> J2 -> strip and strip return
  J2.1 -> J1.3) is an insulated, strain-relieved bundle outside the perfboard; not a single 1.5 mm²
  wire enters the perfboard. Only 24 AWG feeds do (ESP32 5V/GND about 0.6 A, plus U2 and D1
  branches), and the strip current (about 2.5 A) never passes through 24 AWG.
- One wire per plug screw; at most two conductors per solder joint (a C1 lead counts as one
  conductor). Multi-drop branches are made at J1/J2 header pin tails, at most two wires per tail.

## Current model and limits

- PSU: 5 V 3.8 A (Cortex MA-0504A 5 V 3.8 A).
- Software current model defaults: ma=2500, led_ma=60, led_idle=1000, psu_pct=80.
- Firmware example psu_ma 3450 (3800-350): 3450 x 80% = 2760, effective budget
  min(2500, 2760) = 2500 mA.
- Hardware plan psu_ma 3200 (3800-600, a 600 mA controller/peripheral reserve
  [calculation]): 3200 x 80% = 2560, effective 2500 mA; total about 3100 mA with a 700 mA label
  margin [calculation]. The plan is not applied by software; the builder enters psu_ma from own
  measurement.
- Effective strip budget: 2500 mA.
- The 20 mA per LED figure is not universal: 120 x 60 = 7.2 A RGB planning demand, 7.32 A with the
  idle term. Neither is a load the 3.8 A PSU will deliver.
- `ledtest 4` produces fixed RGB(32,32,32), independent of bri, limiter active: about 1023 mA
  model demand in the 120/60/1000 model. It is not a 3.75 A performance proof.
- The software limiter models current; it does not measure current and it is not fault protection.
  Wiring and terminations are sized for the 3.8 A source plus the T5 A fault regime.
- Fuse F1 is T5 A.

## Bill of materials

Part classes say where each part came from; they are NOT parts a builder may omit:

- Z = required core parts.
- E1 = supplementary parts, originally on hand.
- E2 = protection parts: fuse, diodes, ferrite, socket.
- O = feature parts: rocker switch, IR receiver + 17-key handset, antenna (the netlist writes this class
  with the Turkish letter O with umlaut).

Every listed reference is fitted, quantity 1 each: 32 references in total.

### Class Z

| Ref | Qty | Value | Location |
|---|---:|---|---|
| PSU1 | 1 | Cortex MA-0504A 5 V 3.8 A | External module/harness |
| J1 | 1 | T11933 male 90° + T11927 female plug | External module/harness |
| J2 | 1 | T11933 male 90° + T11927 female plug | External module/harness |
| STRIP1 | 1 | WS2812B 2 m / 120 LED | External module/harness |
| U2 | 1 | SN74HC00N | Perfboard |
| D4 | 1 | 1N4007 | Perfboard |
| D5 | 1 | 1N4007 | Perfboard |
| C2 | 1 | 47 nF | Perfboard |
| R1 | 1 | 220 Ω | Perfboard |
| U1 | 1 | ESP32 DevKit 38 pin | Perfboard |
| DSP1 | 1 | ST7789 1.3″ | External module/harness |
| MIC1 | 1 | MAX4466 module | External module/harness |
| R6 | 1 | 1 kΩ | Perfboard |
| C3 | 1 | 10 nF | Perfboard |

### Class E1

| Ref | Qty | Value | Location |
|---|---:|---|---|
| C1 | 1 | 1000 µF / 16 V | External module/harness |
| R2 | 1 | 10 kΩ | Perfboard |
| R3 | 1 | 47 Ω | Perfboard |
| R4 | 1 | 47 Ω | Perfboard |
| R5 | 1 | 47 Ω | Perfboard |
| C6 | 1 | 10 µF / 16 V | Perfboard |
| R7 | 1 | 47 Ω | Perfboard |
| C4 | 1 | 10 µF / 16 V | Perfboard |
| C5 | 1 | 47 nF | Perfboard |
| C7 | 1 | 100 µF / 16 V | Perfboard |

### Class E2

| Ref | Qty | Value | Location |
|---|---:|---|---|
| F1 | 1 | T5 A 5×20 mm glass fuse + holder | External module/harness |
| D1 | 1 | 1N4007 (crowbar) | Perfboard |
| D2 | 1 | 1N5819 | Perfboard |
| FB1 | 1 | Axial ferrite ≥0.5 A · 30–600 Ω at 100 MHz | Perfboard |
| SK1 | 1 | DIP-14 IC socket | Under U2 |

### Class O

| Ref | Qty | Value | Location |
|---|---:|---|---|
| SW1 | 1 | Panel rocker switch | External module/harness |
| IR1 | 1 | IR receiver module | External module/harness |
| ANT1 | 1 | 2.4 GHz / 50 Ω antenna | External module/harness |

### Totals and consumables

- 1N4007 x3 in total (D1, D4, D5); 1N5819 x1 (D2).
- J1/J2 are two connector sets: two headers (T11933) + two matching screw plugs (T11927), so four
  sub-parts, not four sets. Headers are outside the perfboard: thick wires solder to the header pin
  tails, PSU and strip wires go into the plug screws (one wire per screw).
- Other values: 47 Ω x4; 10 µF x2; 47 nF x2; 10 nF / 100 µF / 1000 µF one each. One suitable
  5×20 mm holder for F1; one 17-key handset for the IR kit. U1's on-board D3/BOOT/bypass parts are
  not counted separately.
- C1 is off-board: its two leads solder to the strip +5V and GND input pads together with the
  1.5 mm² wires (wire + lead = two conductors per joint); long lead (+) to V5.
- Wire totals from the cutting schedule: 0.470 m of 1.5 mm² and 4.090 m of 24 AWG including cut
  allowance; the thin total includes 13 module cables x 150 mm. Every wire is listed with its route
  plus at least 20 mm end allowance. These are cutting-schedule figures, not measured installed
  lengths; outer case and strip runs are cut to the user's measurement and the total is recalculated
  if the separate cut allowance is exceeded.
- Consumables as listed: heat-shrink for the J1/J2 pin tails (diameter [unverified]; 3 tails on
  J1, 4 on J2), a cable tie for every 1.5 mm² wire and for the header body, hot glue/adhesive and a
  permanent marker. Solder, flux, ventilation, soldering iron, wire strippers, multimeter and
  calipers should be ready. No extra terminal, WAGO or lug is needed; 1.5 mm² ferrules are
  optional: without one, twist the stripped wire tightly into the screw and do not solder in the
  screw. An oscilloscope is useful for analog clipping and pulse checks; its absence is recorded.

## Safety notes

1. Separate the PSU and USB and let the capacitors discharge; no ohm or solder work until DC below
   0.1 V is seen. Never discharge a capacitor with a screwdriver. Voltmeter lead stays in the V/Ω
   jack with COM black; never touch a rail with a probe in the current jack.
2. Never open the PSU mains case. Check the fuse only with a separate de-energized continuity
   test. Never apply a live short or reverse-polarity experiment to test the fuse; F1/D1/PSU fault
   coordination is [unverified].
3. J1 and J2 must not be confused (safety). Cut the NC pin 2 of the J1 header flush before
   soldering and fill cavity 2 of the PSU plug with a tight insulated plug; then the J2.2 DIN pin
   stops the PSU plug seating in J2 [unverified, verify by K8 dry fit]. Label header and plugs
   "J1 PSU" / "J2 STRIP" and run the identification test (PSU off, plugs out, U1/U2 not fitted, F1
   fitted, SW1 OFF, diode mode at the header pin tails): J1 red 3 / black 1 about 0.4-0.7 V and
   stable for 3 s; J1 reversed OL; J2 red 1 / black 3 OL; J2 red 3 / black 1 rises to OL as C7
   charges. If any reading differs, the headers are mixed up: fix the labels and do not mate the
   PSU plug. With the PSU on and the plug on no header, +5.0-5.2 V between plug screws 1 and 3
   (1 positive) identifies the PSU plug. The PSU plug is never mated before both layers pass.
4. Fix every J1/J2 header body to a rigid point (case wall or a free corner hole of the perfboard)
   with cable tie or hot glue so mating force never reaches the solder joints. Cover every pin tail
   solder with heat-shrink before any mating (J1 tails 1/3/4, all four J2 tails), bind each 1.5 mm²
   wire to the body within about 30 mm, and mate/unmate plugs only with power off while holding the
   header body.
5. One wire per plug screw; at most two conductors per solder joint (a C1 lead counts as one). No
   third wire and no WAGO or distribution joint; make multi-drop branches at header pin tails.
6. The 1.5 mm² harness (1.382 mm equivalent bare diameter) is never forced into the typical
   0.94-1.0 mm perfboard holes and its conductor is never thinned. Thick wire connects only to a
   plug screw, header pin tail, F1 socket solder lug, SW1 blade lug or PSU/strip end; a hole or pad
   chain is not a current path. Only 24 AWG feeds enter the perfboard, on the solder side.
7. F1 (T5 A) protects the 1.5 mm² harness only, not the 24 AWG branches (SW1.1->D1.K,
   SW1.2->D2.A/D4.A, J1.4->GND chain). Keep branches short, insulated and safe from movement; the
   builder checks branch faults.
8. D2 blocks the USB -> strip path only; it does not by itself protect the PC from the PSU. Confirm
   the U1 on-board D3 direction and T9 (VBUS < 0.3 V with no PC connected) before using PSU and PC
   together. If unsure, do not connect PSU+PC. After confirmation, power up PSU -> USB and shut
   down USB -> PSU. With SW1 open, USB may still power U1.
9. Program the isolated U1 over USB only: remove every external signal and breadboard connection
   and any wire on GPIO4 first, reconnect only without power. A successful compile makes neither
   the binary on the board nor the attached hardware safe.
10. Never power the full strip from USB.
11. Never connect U2 (SN74HC00N) directly to 5 V: U2 is fed only through the two 1N4007 drops
    D4/D5, and there is no diode-bridge or alternative feed.
12. The SW1 lamp lead is insulated NC. An AC marking does not prove DC suitability; K11 verifies
    the DC rating. For first strip tests lay the strip out open (no high load on the reel) and
    never run a full unlimited white experiment.

## Acceptance gates

All gates are open until the builder measures them; no software check ever closes them.

### K1-K11: part and fit gates

| Gate | Check |
|---|---|
| K1 F1 | Real T5 A 5×20 mm glass fuse and suitable holder verified by code; a PTC is not the same. Separate de-energized fuse/holder continuity, DC suitability and contact check. F1/D1/PSU foldback coordination is not guaranteed. |
| K2 U2/FB1/SK1 | U2 is SN74HC00N, 14 legs, TI notch orientation; socket DIP-14. FB1 is a through-hole axial ferrite ≥0.5 A, 30–600 Ω at 100 MHz; record real code, impedance and DCR. Not a coil, not a bridge. |
| K3 perfboard | 110 × 65 mm; 40 x X holes with X=25 [unverified]; 65/2.54 about 25.59 is not a hole count. 25 rows span 60.96 mm (edge margin 4.04 mm), 40 columns span 99.06 mm (edge margin 10.94 mm); the drawn solid frame is the hole area 40 x 2.54 = 101.6 mm by 25 x 2.54 = 63.5 mm. Measure real rows/columns, edge distances, hole diameter, copper layout, U1 row pitch, all bodies, socket/USB/coax and trimpot access. Physical fit [unverified]; on any difference regenerate the model with the measured numbers before drilling or soldering. |
| K4 D2/D3 | D2 code 1N5819, diode test A -> banded K forward, reverse OL. Confirm U1's on-board D3 direction from the board schematic, then T9 with no PC. D2 only blocks the USB -> strip path. |
| K5 PSU | Label 5 V 3.8 A; measure the low-voltage output unloaded and at accepted load. T1 5.0-5.2 V; above 5.2 V nothing is connected. |
| K6 U1/ANT1 | 38-pin DevKit, 5V/VIN and GND prints, regulator identity, dropout and heating, plus the manufacturer's required on-board bypass (C7 does not compensate). WROOM-32U/u.FL matches a 2.4 GHz / 50 Ω antenna and connector; no PCB trace is cut. With WROVER/PSRAM, GPIO16 is not used for IR and no other pin is guessed. |
| K7 wires/values | 1.5 mm² copper cross-section, equivalent bare diameter 1.382 mm; 24 AWG about 0.205 mm² / 0.511 mm. Insulation diameter is not used to derive cross-section. R1=220 Ω, R2=10 kΩ, R3/R4/R5/R7=47 Ω, R6=1 kΩ; capacitor codes and polarities measured separately. |
| K8 terminals | PSU +/COM, strip DIN arrow, TFT/MIC/IR print order verified; never guess a pin from color. Header body support and both J1/J2 mix-up layers (labels, dry fit, identification test) pass. One wire per screw, at most two conductors per solder joint. |
| K9 firmware | Firmware configures GPIO4 INPUT_PULLDOWN only and keeps BLK19/IR16. Install only a binary you built and checked, with U1 isolated; GPIO4 stays externally NC. |
| K10 IR/SW1 | IR VCC/GND/OUT order and 3.3 V compatibility; NEC addresses/codes confirmed with `ir learn on`. SW1 shows two real contacts switching open to low ohm; lamp lead insulated. An AC label is not DC proof. |
| K11 real load | SW1 DC suitability, contact drop ≤ 0.10 V at the real acceptance load and contact heating; current and conductor ratings of every terminal plus strain relief. T1-T11, both HC interface HIGH/LOW margins, ESP5V/3V3 headroom and heating. An unloaded measurement is not accepted. |

### T1-T11: measurement gates

| Test | Measurement / acceptance |
|---|---|
| T1 | J1 +/GND 5.0-5.2 V, unloaded and at measured load. |
| T2 | J2.1 -> J1.3 GND drop absolute < 0.05 V, strip return measured separately. |
| T3 | J2.3/J2.1 ≥ 4.6 V at measured load. Not an ESP32 supply acceptance by itself. |
| T4 | U2.14/U2.7 3.6-4.2 V; at the real local rails GPIO25 HIGH > HC VIH and LOW < HC VIL; U2 DIN HIGH > first pixel VIH and LOW < VIL; measured margins recorded. The typical two-diode calculation is no guarantee at all corners. |
| T5 | U1 3V3/GND 3.2-3.4 V with the real D2 drop, ESP5V and the real regulator dropout, heating and bypass conditions. C7 fitted does not close a headroom failure. |
| T6 | With U1 unmounted, the GND/DGND/AGND external paths are separate. After U1 is fitted, internal GND continuity is normal; do not re-pass this test as proof. |
| T7 | IR pin order, model and 3.3 V; real NEC codes with `ir learn on` / `ir learn off`. If WROVER appears, stop. |
| T8 | MIC OUT silent bias about 1.45-1.85 V; record min/max and clipping, then follow the analog gain procedure below. |
| T9 | With no PC connected and the board on the correct PSU branch: USB VBUS < 0.3 V, plus D3 direction and schematic confirmation. If unsure or high, no PSU+PC together. |
| T10 | First data/module and real-load display and HC signals; smooth low-light operation is not high-load evidence. |
| T11 | Closed SW1 contacts ≤ 0.10 V at the real acceptance load; contact and terminal heating and strain relief. Lamp NC. |

## MAX4466 gain adjustment

- No extra 10 kΩ/100 kΩ pot is added: use the module's existing accessible trimpot. Mark the
  current position and, with power off, fix the module so the trimpot stays reachable.
- Start at low analog gain and do not force the mechanical end. This clone's rotation direction and
  gain range are [unverified]: do not assume clockwise is high or low, find it from the signal.
- Use silent, normal and loud expected material at the same normal listening distance; at each
  candidate record supply condition, trimpot mark, `selftest` MIC/raw min/max, bias and ADC rail
  clip count.
- Raise the gain only as far as useful signal needs. Strip movement caused by noise is not a
  sensitivity success. Flat tops or distortion on loud signal and asymmetric bias are analog
  problems; MAX4466 saturation can happen before the ADC rail.
- Without waveform equipment, write down that limit: "no ADC clip" is not proof of no analog
  clipping.
- At the highest attenuation avoid the ESP32 ADC region above about 2.45 V; watch the bias too.
- Software AGC and gain only scale the visualization; they cannot reconstruct analog-clipped or
  band-limited samples.
- After the final analog adjustment, HASH held ≥ 3 s or CLI `cal` requests the unchanged
  two-second calibration.

## Programming

- Build only with [tools/build_fw.sh](../tools/build_fw.sh): it compiles and never writes the
  board.
- Install deliberately with [tools/flash.sh](../tools/flash.sh) over USB; it writes the bootloader
  at 0x1000, the partition table at 0x8000, otadata at 0xe000 and the application at 0x10000.
- Flashing never writes or erases the saved-settings (NVS) region 0x9000-0xDFFF.
- Never use a merged or full-flash image that covers NVS, an erase-flash, or a restore of someone
  else's NVS record.
- Remote and serial control are documented in [controls.md](controls.md) and [cli.md](cli.md).
