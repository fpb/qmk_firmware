# AJAZZ AK820 PRO

![AK820 PRO](https://i.postimg.cc/tJ17FD6g/ak820pro.jpg)

AJAZZ AK820 Pro hotswap triple mode, rgb, PID 0x8009, VID 0x0C45

- MCU: HFD80CP100 (rebranded Sonix SN32F299)
- PCB: SG8975-RGB-HFD BT2.4G; REV: 01; 2023/07/08
- Wired only, no bluetooth and no 2.4G support.


Keyboard Maintainer: [fpb](https://github.com/fpb)


Note: keymap may differ slightly from stock firmware for FN combinations

See [keymap.c](keymaps/default/keymap.c), and refer to the [list
of QMK keycodes](https://beta.docs.qmk.fm/using-qmk/simple-keycodes/keycodes).


Building instructions
-----------------


Make example for this keyboard (after setting up your build environment):

    $ make a_jazz/ak820pro:all

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader:

There are two pins under the SPACE bar. They are covered by 2 insulation layers and 1 removable foam strip (there are two strips on each side of the space switch that are easily removable). Cutting a window on the 2 insulation layers will give access to the pins. Shorting them while connecting the USB cable will make the MCU enter bootloader mode. In this mode the USB VID/PID will be 0x0C45/0x7140.

![Bootloader mode](https://github.com/fpb/ajazz-ak820-pro/blob/main/img/bootloader-pins.jpg)

