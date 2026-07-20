# The GC9107 panel + dashboard are driven entirely bare-metal (graphics/lcd_bus.c):
# we own SPI0 (+ Vector58) for the interrupt-driven flash->LCD DMA, and decode QP's
# qgf/qff asset blobs ourselves. No Quantum Painter runtime -- its concurrent main-loop
# activity corrupted the background DMA. See docs/LCD_FLASH_LAYER.md.
SRC   += graphics/lcd_bus.c

# CH582F wireless module exposed through QMK's official Bluetooth driver API.
# BLUETOOTH_DRIVER = custom defines BLUETOOTH_ENABLE, CONNECTION_ENABLE and
# NO_USB_STARTUP_CHECK and compiles bluetooth.c (weak bluetooth_* defaults);
# our ch582f_ajazz.c (below in SRC) provides the strong overrides.
BLUETOOTH_ENABLE = yes
BLUETOOTH_DRIVER = custom

# WIP: external PCF8563 RTC over the ChibiOS software (bit-banged) I2C fallback LLD.
# Swaps the SN32 HW I2C driver for the SW fallback; rtc.c drives it via the I2C HAL
# API. Does NOT work on hardware yet (compiles/links fine) -- see rtc.c.
USE_HAL_I2C_FALLBACK = yes

SRC += bluetooth/ch582f_ajazz.c

# Dashboard graphics: splash, big clock font (Iosevka 30), small status font
# (Roboto Mono 20), and the top-row Mac/Win + connection icons.
# Generated from the source PNGs by res/mkraw.py --embed (RGB565 tiles).
SRC += graphics/res/lcd_assets.c

SRC += graphics/display.c
SRC += rtc/rtc.c
VPATH += bluetooth
VPATH += graphics
VPATH += rtc
