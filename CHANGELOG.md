# Changelog

## 1.0.0

First public release.

- Portable integer C++17 core: decimation, 1024-point FFT, 40-band analyzer with AGC and noise calibration, nine LED modes with gamma, auto exposure and a current limiter, and a 240×240 screen renderer.
- ESP32 firmware: continuous ADC sampling, ST7789 and WS2812B drivers, IR remote control, USB serial CLI, saved settings in NVS, self-test. One Arduino application on the standard No OTA partition scheme; no Wi-Fi.
- Host tests, golden references and an electrical-contract test for the v2 netlist and firmware configuration.
- Interactive preview (`docs/index.html`, GitHub Pages): the real core compiled to WebAssembly analyzes a local audio file or a built-in demo live in the browser and drives all nine LED modes at once, with analyzer readouts and an emulated display.
- Hardware design B (v2.3): netlist, schematic and perfboard drawings, bill of materials and the K1–K11 / T1–T11 acceptance gates (`docs/hardware.md`). Physical assembly is not accepted by any test.
- English documentation: hardware, serial CLI reference and controls.
