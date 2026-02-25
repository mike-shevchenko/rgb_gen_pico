---------------------------------------------------------------------------------------------------
# Overview

rgb_gen_pico - RGB video signal generator based on RPi Pico (RP2040). Intended as a cheap
TV-compatible signal generator for retro TV/monitor servicemen.

Currently, generates a static image which has the video timings of a Soviet computer Agat-7. Other
platforms (ZX Spectrum clones, BK-0010/0011 (БК-0010/0011) may follow.

The project was forked from the Github public repo:
https://github.com/osemenyuk-114/zx-rgbi-to-vga-hdmi-PICOSDK
The original project is an RGBI converter to VGA or HDMI. Some of its code was reused with the help
of the LLM Claude Sonnet 4.

---------------------------------------------------------------------------------------------------
# Build and flash

Install VS Code, and its official `Raspberry Pi Pico` extension.

Open the project folder with VS Code. When asked (at the top of the window) to "Select a kit",
select `Pico`. After seeing the message (at the bottom of the window)
`[cmake] -- Build files have been written to...`, click the `Compile` button at the bottom right,
and the file `build/rgb_gen_pico.uf2` will appear.

Connect the board to USB and to a TV/Monitor (see below) - a folder associated with the board will
open (as Mass Storage) - drag'n'drop the built .uf2 file to this folder, it will be closed and the
firmware will start working immediately - enjoy the picture.

---------------------------------------------------------------------------------------------------
# Hardware

The firmware is compatible with any of the following boards:
- WaveShare RP2040-Zero
- Original Raspberry Pi Pico
- [Murmulator](https://murmulator.ru/types)
- [Murmulator-based RGB-to-VGA/HDMI](https://murmulator.ru/rgbtovgahdmi)

## Connect the monitor/TV:

Use either of the following TV/monitors:
- TV/monitor with the SCART port that supports RGB.
- Sony PVM or the like with a BNC (Bayonet) RGB inputs.
- TV/monitor with an Agat-specific DIN-7 port: 32VTC (32ВТЦ), Yunost (Юность), Silelis (Шилялис).

NOTE: The intensity (brightness) bit/pin is not currently supported by the firmware.

### Murmulator boards with the VGA connector:

#### SCART/BNC:
```
GND       --------------> GND (any of SCART pins 5, 9, 13, 18)
3V3 or 5V ---[150]------> Blank (SCART-only: SCART pin 16)
VGA 1 R   --------------> R (SCART pin 15)
VGA 2 G   --------------> G (SCART pin 11)
VGA 3 B   --------------> B (SCART pin 7)
VGA 13 HS ---|<|--+
                  |
VGA 14 VS ---|<|--+-----> Sync (SCART pin 20)
```

#### DIN-7 for Agat:
```
VGA 4 GND --------------> GND (DIN-7 pin 2)
VGA 1 R   --------------> R (DIN-7 pin 3)
VGA 2 G   --------------> G (DIN-7 pin 6)
VGA 3 B   --------------> B (DIN-7 pin 1)
VGA 13 HS --------------> HS (DIN-7 pin 5)
VGA 14 VS --------------> VS (DIN-7 pin 4)
```

### RPi Pico or WaveShare RP2040:

#### SCART/BNC:
```
GND       --------------> GND (any of SCART pins 5, 9, 13, 18)
3V3 or 5V ---[150]------> Blank (SCART-only: SCART pin 16)

GP6 BL    ---[ 1k]--+
                    |
GP7 BH    ---[430]--+---> B (SCART pin 7)

GP8 GL    ---[ 1k]--+
                    |
GP9 GH    ---[430]--+---> G (SCART pin 11)

GP10 RL   ---[ 1k]--+
                    |
GP11 RH   ---[430]--+---> R (SCART pin 15)

GP12 HS   ----|<|---+
                    |
GP13 VS   ----|<|---+---> Sync (SCART pin 20)
```

#### DIN-7 for Agat:
```
GND     --------------> GND (DIN-7 pin 2)
GP7 BH  ---[330]------> R (DIN-7 pin 3)
GP9 GH  ---[330]------> G (DIN-7 pin 6)
GP11 RH ---[330]------> B (DIN-7 pin 1)
GP12 HS --------------> HS (DIN-7 pin 5)
GP13 VS --------------> VS (DIN-7 pin 4)
```
