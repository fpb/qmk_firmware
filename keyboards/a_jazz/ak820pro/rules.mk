QUANTUM_PAINTER_ENABLE = yes
QUANTUM_PAINTER_DRIVERS += gc9107_spi

# CH582F wireless module exposed through QMK's official Bluetooth driver API.
# BLUETOOTH_DRIVER = custom defines BLUETOOTH_ENABLE, CONNECTION_ENABLE and
# NO_USB_STARTUP_CHECK and compiles bluetooth.c (weak bluetooth_* defaults);
# our ch582f_ajazz.c (below in SRC) provides the strong overrides.
BLUETOOTH_ENABLE = yes
BLUETOOTH_DRIVER = custom

# Dashboard graphics: splash, big clock font (Iosevka 30), small status font
# (Roboto Mono 20), and the top-row Mac/Win + connection icons.
SRC += graphics/sonixqmk.qgf.c
SRC += graphics/Iosevka-Regular-30.qff.c
SRC += graphics/robotomono20.qff.c

SRC += graphics/apple_icon_24x24.qgf.c
SRC += graphics/windows_icon_24x24.qgf.c
SRC += graphics/cable_icon_24x24.qgf.c
SRC += graphics/bluetooth_icon_24x24.qgf.c
SRC += graphics/2_4_g_icon_24x24.qgf.c

SRC += display.c
SRC += bluetooth/ch582f_ajazz.c
VPATH += $(KEYBOARD_PATH_1)/bluetooth
