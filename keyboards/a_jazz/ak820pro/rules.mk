QUANTUM_PAINTER_ENABLE = yes
QUANTUM_PAINTER_DRIVERS += gc9107_spi

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
SRC += graphics/res/sonixqmk.qgf.c
SRC += graphics/res/Iosevka-Regular-30.qff.c
SRC += graphics/res/Iosevka-Medium-20.qff.c
SRC += graphics/res/apple_icon_24x24.qgf.c
SRC += graphics/res/windows_icon_24x24.qgf.c
SRC += graphics/res/cable_icon_24x24.qgf.c
SRC += graphics/res/bluetooth_icon_24x24.qgf.c
SRC += graphics/res/2_4_g_icon_24x24.qgf.c

SRC += graphics/display.c
SRC += rtc/rtc.c
VPATH += bluetooth
VPATH += graphics
VPATH += rtc
