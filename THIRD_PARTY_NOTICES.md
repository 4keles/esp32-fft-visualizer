# Third-party notices

The project is licensed under the MIT License (see [LICENSE](LICENSE)). The license covers the code and documents contributed by this repository, not third-party material or recordings. No audio is distributed with this repository.

## Musical Christmas Lights (Dilum Sanjaya)

Source: https://codepen.io/dilums/pen/MWjEqaa

The LED modes were derived from the logic of this CodePen: hue follows frequency, brightness follows a thresholded band level, and the brightness mapping follows a dot-size-squared law (`cpBright` in `tools/gen_vis_tables.cpp`). The additional modes, the integer implementation and the hardware drivers are this project's own. The upstream demo files and its music credits are not part of this repository; no music from that pen is bundled.

The upstream license notice is reproduced in full as required:

```text
The MIT License (MIT)

Copyright (c) 2026 Dilum Sanjaya (https://codepen.io/dilums/pen/MWjEqaa)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Dependencies and documentation sources

These are used to build, run or document the project. They are not redistributed here, and naming them is not an endorsement.

- Espressif Systems: the Arduino core for the ESP32 (3.3.12) and the ESP32 platform libraries that the firmware is compiled against.
- Arduino CLI: the build and upload tool called by `tools/build_fw.sh` and `tools/flash.sh`.
- Chip and module vendors, whose datasheets are the source of the electrical limits quoted in `docs/hardware.md`: Espressif (ESP32), Analog Devices / Maxim (MAX4466), Worldsemi (WS2812B), Sitronix (ST7789) and Texas Instruments (SN74HC00).

## Font table

`tools/gen_vis_tables.cpp` contains a 5×7 bitmap font table. Its origin is not documented in this project, so no authorship or license is claimed for it beyond what its source may carry.

## Audio

The interactive preview in `docs/` plays either a demo synthesized by the core at run time or a file the visitor picks. Picked files are decoded and analyzed inside the visitor's browser and are never uploaded. No recording is distributed with this repository.
