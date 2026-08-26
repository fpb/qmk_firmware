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
    - Connection type (USB/BT/2.4G) + Slot (for BT mode)
    - date (day/month)
    - time (HH:MM or HH:MM:SS)
    - battery charge level (%)
- [x] Triple mode support (Use `Fn`+[`Q`|`W`|`E`] for Bluetooth and `Fn`+`R` for 2.4G dongle. Keep pressed for pairing.
- [x] Per-key RGB Matrix (hardware PWM across CT16B0/B1/B2 — see `hardware_pwm.diff`)
- [x] Play animations from flash memory
- [x] Via support
- [x] Support utility to flash assets to Flash memory and to set the clock

Keyboard Maintainer: [fpb](https://github.com/fpb)

-----------------
## Building instructions


Make example for this keyboard (after setting up your build environment):

    $ qmk compile -kb a_jazz/ak820pro -km default

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

### What this branch is (unified-driver experiment)

`ak820pro-flashlcd-unified` is an experiment sibling of `ak820pro-flashlcd-tiles`.
It draws the **identical** flash-tile dashboard, but instead of owning SPI0
bare-metal it drives the panel entirely through the **ChibiOS SN32 SPI driver**:
all panel commands/pixels go through `spiSend` (FIFO-batched by `spi_fifo_pump.diff`)
and the flash→LCD DMA is the driver's `spiSN32FlashDma*` extension
(`spi_flash_dma.diff`). SPI1 (flash reads) stays bare-metal. `Vector58`,
`spi0_setup`, and the pipelined `tx_pipe` are gone from `graphics/lcd_bus.c`.

**Question it answers:** can one SPI driver replace the bare-metal bus without
losing throughput? **Measured on hardware (vs `tiles`):**

| | tiles (bare-metal) | unified (driver) |
| --- | --- | --- |
| flash | 76080 B | 76548 B (**+468**) |
| idle matrix scan | ~1392 Hz | 1393 Hz |
| full-redraw dip | — | 1361 Hz (~−2%) |
| dashboard / animation | ✅ | ✅ (both render) |

**Verdict: a wash.** No measurable speed cost, +468 bytes, one SPI driver instead
of a bare-metal `Vector58` fork. The CPU byte-swap in `tx_pixels`/`lcd_fill_rect`
(a `uint16` RGB565 tile is little-endian; the panel wants hi-byte-first) is *not*
on the hot path — the dashboard is 100% flash-DMA, so those helpers are unused.
`tiles` remains the shipping branch (minimal, no `HAL_USE_SPI` dependency); this
branch is kept as a working reference that the stage-1 pump + stage-2a DMA
extension can drive the whole panel.

### ChibiOS submodule patches (all five required)

This branch needs **five** working-tree patches applied to `lib/chibios-contrib`
before building. They touch disjoint files, so order does not matter:

    $ cd lib/chibios-contrib
    $ git apply ../../keyboards/a_jazz/ak820pro/hardware_pwm.diff
    $ git apply ../../keyboards/a_jazz/ak820pro/i2c_fallback.diff
    $ git apply ../../keyboards/a_jazz/ak820pro/rtc_lld.diff
    $ git apply ../../keyboards/a_jazz/ak820pro/spi_fifo_pump.diff
    $ git apply ../../keyboards/a_jazz/ak820pro/spi_flash_dma.diff

**`hardware_pwm.diff`** — multi-timer hardware PWM for the RGB matrix. The 15 columns exceed CT16B1's 12 PWM channels, so they are spread over CT16B0/B1/B2. This generalises the SN32 CT PWM LLD (adds PWMD0/PWMD2, the SN32F290 PWMCTRL-only/key-protected register model, MR9 period register) and moves the ChibiOS OS-tick counter off CT16B0 to the otherwise-unused CT16B5 (so CT16B0 is free for PWM — column C14 can only route there).

**`i2c_fallback.diff`** — RTC over the ChibiOS software (bit-banged) I2C fallback LLD. The LCD clock is driven by an external PCF8563/D8563 RTC on P0.14/P0.15 — pins the SN32 hardware I2C peripheral cannot reach, so this port uses the fallback LLD (`USE_HAL_I2C_FALLBACK = yes`). The stock fallback driver does **not** work on this board (in `OPENDRAIN=FALSE` mode it releases SCL to input each clock, which the SN32 reads back low, so every transfer times out). This patch keeps SCL push-pull throughout, idles the pins in `i2c_lld_start`, and cleans up the SDA read/release ordering.

**`rtc_lld.diff`** — the SN32 RTC LLD itself (`hal_rtc_lld.c/.h`), plus the `platform.mk` line that pulls `RTC/driver.mk` into the build. Distinct from `i2c_fallback.diff`, which fixes the *I2C* path to the external PCF8563; this one is the ChibiOS RTC driver for the SN32's own on-chip RTC.

**`spi_fifo_pump.diff`** — FIFO-batched pump for the ChibiOS SN32 SPI driver (`hal_spi_v2_lld`). The stock pump sends one byte per interrupt and waits for its RX word before the next, so SCLK sits idle for the whole IRQ latency between bytes. This primes the TX FIFO on kick-off and drains-all-RX + refills-to-full per IRQ (`RXFIFOTH=0` for tail delivery). It brings the driver up to what this port's bare-metal `tx_pipe` already did — which is why the unified branch loses no throughput. Also on `-qp-lld` and `-full`.

**`spi_flash_dma.diff`** — the SPI-to-SPI flash→LCD DMA as a driver extension (`spiSN32FlashDmaPrepare`/`Fire`/`Busy`). The DMA registers live on SPI0 itself, so the driver borrows SPI0 for the data phase (8-bit command config → 16-bit pixel words), arms it, and services completion in its own SPI0 handler — restoring 8-bit **and the FIFO-mode interrupt enable** so a following `spiSend` still completes (this branch drives the driver directly between DMAs, unlike `-qp-lld` which re-runs `spiStart` per QP flush). `graphics/lcd_bus.c` just calls the API. Gated by `SN32_SPI0_FLASH_DMA` (`mcuconf.h`). This is the same extension as `-qp-lld`'s, plus the IE-restore.

Note: all five are working-tree edits of the `lib/chibios-contrib` submodule and are discarded by `git submodule update`; re-apply if that happens. The submodule working tree is shared across branches, so after switching branches you may already have another branch's patches applied — `git -C lib/chibios-contrib status` shows what is live.

## Bootloader

Enter the bootloader:

There are two pins under the SPACE bar, to the right of the switch. They are covered by 2 insulation layers and 1 removable foam strip (there are two strips on each side of the space switch that are easily removable). Cutting a window on the 2 insulation layers will give access to the pins. Shorting them while connecting the USB cable will make the MCU enter bootloader mode. In this mode the USB VID/PID will be 0x0C45/0x7140.

After flashing QMK firmware you can simply press `ESC` while plugging the cable or `Fn`+`ESC` to enter flashing mode.

## Tools required

Additionally you may want to use:

- [SonixFlasherC](https://github.com/SonixQMK/SonixFlasherC) to flash the firmware. For a working verision on MacOS Tahoe you may use [this branch of my fork](https://github.com/fpb/SonixFlasherC/tree/fix_for_macos_tahoe).
- [**ak820ctl** (time-util-ak820pro)](https://github.com/fpb/time-util-ak820pro) — the host toolkit: set the LCD clock, and (this `tiles` branch only) build and flash the LCD image assets and GIF animations into external SPI flash. The asset-authoring pipeline (source PNGs + `mkraw.py`/`mkanim.py`) lives there too; the firmware tree keeps only the generated `graphics/res/flash_assets.h`.

