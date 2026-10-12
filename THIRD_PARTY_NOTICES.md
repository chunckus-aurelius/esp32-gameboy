# Third-party notices

This repository's own code is MIT-licensed (see `LICENSE`). The Game Boy core
under `src/gb_core/vendor` is derived from the projects below. Exact file
provenance and hashes are in `vendor-provenance/core.json`.

The core was originally obtained through Wenting Zhang's PaperBoy port
(https://gitlab.com/zephray/paperboy, commit `e70b2936`). No PaperBoy-original
code is included; the PaperBoy-only glue header was replaced by
`src/gb_core/vendor/esp32_crankboy_port.h`, written for this repository.

## CrankBoy

Source: https://github.com/CrankBoyHQ/crankboy-app (MIT)

Derived files:

- `src/gb_core/vendor/peanut_gb.h`
- `src/gb_core/vendor/peanut_gb_core.h`
- `src/gb_core/vendor/pgb/pgb_common.h`
- `src/gb_core/vendor/pgb/pgb_v1.h` to `pgb_v4.h`

Playdate-specific code removed; adapted for ESP32. `reverse_bits_u8` and
`reverse_bits_in_each_byte_conditional_u16` in `esp32_crankboy_port.h` are
adapted from CrankBoy `src/utility.h`.

CrankBoy's license, which also covers the Peanut-GB and SameBoy copyrights it
carries:

```
MIT License

Copyright © 2024-2025, CrankBoy contributors
Copyright © 2023-2024, Risolvi Productions
Copyright © 2023, Paul Sorensen
Copyright © 2018-2022, Mahyar Koshkouei
Copyright © 2015-2019, Lior Halphon

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
SOFTWARE.```

## Peanut-GB and MiniGB APU

Source: https://github.com/deltabeard/Peanut-GB, `examples/sdl2/minigb_apu`
(MIT). Copyright (c) 2017 Alex Baines, Copyright (c) 2019 Mahyar Koshkouei.
The license text is in `src/gb_core/vendor/minigb_apu/LICENSE`.

Files: `src/gb_core/vendor/minigb_apu/minigb_apu.c`, `minigb_apu.h`. Modified
for ESP32 (IRAM placement of hot functions, local build options).

## SameBoy

Source: https://github.com/LIJI32/SameBoy (MIT). Copyright (c) 2015-2019 Lior
Halphon.

A few snippets in `src/gb_core/vendor/peanut_gb.h` are taken from SameBoy and
marked as such in that file, whose header carries this attribution. The MIT
terms are the same as above.
