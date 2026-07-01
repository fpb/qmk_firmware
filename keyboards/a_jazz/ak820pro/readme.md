# AJAZZ AK820 PRO

![AK820 PRO](https://i.postimg.cc/T3XwwLTN/PXL-20260630-155003679.jpg)

AJAZZ AK820 Pro hotswap triple mode, rgb, PID 0x8009, VID 0x0C45

- MCU: HFD80CP100 (rebranded Sonix SN32F299)
- PCB: SG8975-RGB-HFD BT2.4G; REV: 01; 2023/07/08

For more information about the hardware please check [here](https://github.com/fpb/ajazz-ak820-pro)!

## Status

The following is supported by this port:

- [x] Key matrix
- [x] Encoder
- [x] Mac/Win layouts
- [x] indicator LEDs (CAPS Lock, Windows Lock and Charging)
- [x] LCD Display, showing:
    - Layout (Mac/Win)
    - Connection type (USB/BT/2.4G)
    - date (day/month)
    - time (HH:MM:SS)
    - battery charge level (%)
- [x] Triple mode support (Use `Fn`+[`Q`|`W`|`E`] for Bluetooth and `Fn`+`R` for 2.4G dongle. Press `Fn`+`P` (1 second) to enter pairing mode)

## To Do

Features missing:

 - [ ] RGB Matrix support

Keyboard Maintainer: [fpb](https://github.com/fpb)

-----------------
## Building instructions


Make example for this keyboard (after setting up your build environment):

    $ qmk compile -kb a_jazz/ak820pro -km default

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader:

There are two pins under the SPACE bar, to the right of the switch. They are covered by 2 insulation layers and 1 removable foam strip (there are two strips on each side of the space switch that are easily removable). Cutting a window on the 2 insulation layers will give access to the pins. Shorting them while connecting the USB cable will make the MCU enter bootloader mode. In this mode the USB VID/PID will be 0x0C45/0x7140.

After flashing QMK firmware you can simply press `ESC` while plugging the cable or `Fn`+`ESC` to enter flashing mode.

## Tools required

Additionally you may want to use:

- [SonixFlasherC](https://github.com/SonixQMK/SonixFlasherC) to flash the firmware. For a working verision on MacOS Tahoe you may use [this branch of my fork](https://github.com/fpb/SonixFlasherC/tree/fix_for_macos_tahoe).
- [Utility to set the time](https://github.com/fpb/time-util-ak820pro) on the keyboard.

