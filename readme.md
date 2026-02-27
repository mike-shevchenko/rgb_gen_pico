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

Connect the board to USB and to a TV/Monitor (see below), press and hold the RESET button and then 
press the BOOT (BOOTSEL) button - a folder associated with the board will open (as Mass Storage).

On Windows 10, if the folder does not open, check Device Manager for exclamation mark on the RPi 
USB device - if shown, you can try to install the `libusb` driver via
[VisualGDB USB Driver Tool](https://visualgdb.com/UsbDriverTool).

Then drag'n'drop the built .uf2 file to this folder, it will be closed, and the firmware will start
working immediately - enjoy the picture.

---------------------------------------------------------------------------------------------------
# Hardware

The firmware source code is compatible with any of the following boards:
- WaveShare RP2040-Zero
- Original Raspberry Pi Pico
- [Murmulator](https://murmulator.ru/types)
- [Murmulator-based RGB-to-VGA/HDMI](https://murmulator.ru/rgbtovgahdmi)

ATTENTION: Due to different GPIO assignments, the firmware must be compiled with the proper BOARD 
macro definition (see `main.c`): the Murmulator boards have the VGA connector on GPIO 6 to 13, and
the RGB2VGA boards have it on GPIO 8 to 15. If using a generic board, it is recommended to stick to
the RGB2VGA pinout - this is the default BOARD macro definition.

## Connect the monitor/TV:

Use either of the following TV/monitors:
- TV/monitor with the SCART port that supports RGB.
- Sony PVM or the like with a BNC (Bayonet) RGB inputs.
- TV/monitor with an Agat-specific DIN-7 port: 32VTC (32ВТЦ), Yunost (Юность), Silelis (Шилялис).

NOTE: The intensity (brightness) bit/pin is not currently supported by the firmware.

### Murmulator/RGB2VGA board with the VGA connector:

#### SCART/BNC:
```
3V3 or 5V ---[150]------> Blank (SCART-only: SCART.16)
VGA 4 GND --------------> GND (any of SCART pins 5, 9, 13, 18)
VGA 1 R   --------------> R (SCART.15)
VGA 2 G   --------------> G (SCART.11)
VGA 3 B   --------------> B (SCART.7)
VGA 13 HS ---|>|--+
                  |
VGA 14 VS ---|>|--+-----> Sync (SCART.20)
```

#### DIN-7 for Agat:
```
VGA 4 GND --------------> GND (DIN-7.2)
VGA 1 R   --------------> R (DIN-7.3)
VGA 2 G   --------------> G (DIN-7.6)
VGA 3 B   --------------> B (DIN-7.1)
VGA 13 HS --------------> HS (DIN-7.5)
VGA 14 VS --------------> VS (DIN-7.4)
```

### RPi Pico or WaveShare RP2040:

#### SCART/BNC with brightness support (future-proof):
```
      GND --------------> GND (any of SCART pins 5, 9, 13, 18)
3V3 or 5V ---[150]------> Blank (SCART-only: SCART.16)

   GP8 RL ---[ 1k]--+
                    |
   GP9 RH ---[430]--+---> R (SCART.15)

  GP10 GL ---[ 1k]--+
                    |
  GP11 GH ---[430]--+---> G (SCART.11)

  GP12 BL ---[ 1k]--+
                    |
  GP13 BH ---[430]--+---> B (SCART.7)

  GP14 HS ---[100]---|>|---+
                           |
  GP15 VS ---[100]---|>|---+---> Sync (SCART.20)
```
NOTE: Such connection replicates the schematics of the RGB2VGA boards.

#### SCART/BNC with no brightness support (simplified):
```
      GND --------------> GND (any of SCART pins 5, 9, 13, 18)
3V3 or 5V ---[150]------> Blank (SCART-only: SCART.16)
   GP9 RH ---[270]------> R (SCART.15)
  GP11 GH ---[270]------> G (SCART.11)
  GP13 BH ---[270]------> B (SCART.7)

  GP14 HS ---[1k]---+
                    |
  GP15 VS ---[1k]---+---> Sync (SCART.20)
```

#### DIN-7 for Agat (simplified):
```
GND     --------------> GND (DIN-7.2)
GP9 RH  ---[330]------> B (DIN-7.1)
GP11 GH ---[330]------> G (DIN-7.6)
GP13 BH ---[330]------> R (DIN-7.3)
GP14 HS --------------> HS (DIN-7.5)
GP15 VS --------------> VS (DIN-7.4)
```

#### DIN-7 for Agat (level-shifted):
```
                            SN74HCT244N
                              ---u---
                            *-|1  20|---< +5
                   GP9 RH >---|2  19|-*
                            *-|3  18|---[150]---> R (DIN-7.1)
                  GP11 GH >---|4  17|-
                            *-|5  16|---[150]---> G (DIN-7.6)
                  GP13 BH >---|6  15|-
     VS (DIN-7.4) <---[100]---|7  14|---[150]---> B (DIN-7.3)
                             -|8  13|---< GP15 VS
     HS (DIN-7.5) <---[100]---|9  12|-*
                  GND >---+-*-|10 11|---< GP14 HS 
                          |   -------
    GND (DIN-7.2) <-------+
```

#### DIN-7 for Agat and and DIN-5 (SCART/BNC-compatible) universal:
```
                            SN74HCT244N
                              ---u---
                            *-|1  20|---< +5
                   GP9 RH >---|2  19|-*     
                            *-|3  18|---[150]---> R (DIN-5.3 + DIN-7.1)
                  GP11 GH >---|4  17|-
                            *-|5  16|---[150]---> G (DIN-5.5 + DIN-7.6)
                  GP13 BH >---|6  15|-
                              |     |
     VS (DIN-7.4) <---[100]-+ |     |
S (DIN-5.1) <------+--[470]-+-|7  14|---[150]---> B (DIN-5.4 + DIN-7.3)
                   |         -|8  13|---< GP15 VS
      +5 >--[ 1k]--+--[470]-+-|9  12|-*
     HS (DIN-7.5) <---[100]-+ |     |
                              |     |
GND (DIN-5.2) >-------------*-|10 11|---< GP14 HS 
                              -------
```
