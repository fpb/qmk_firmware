# MCU name
MCU = SN32F248BF
BOOTLOADER = sn32-dfu

TOP_SYMBOLS = yes

# Build Options
#   comment out to disable the options.
#
MAGIC_ENABLE 			= yes
MAGIC_KEYCODE_ENABLE 	= yes
VIA_ENABLE 				= no

EEPROM_DRIVER = wear_leveling
WEAR_LEVELING_DRIVER = sn32_flash

WAIT_FOR_USB 			= no
KEYBOARD_SHARED_EP 		= yes

BLUETOOTH_ENABLE 		= yes
BLUETOOTH_DRIVER		= ItonBT

RGB_MATRIX_CUSTOM_USER = yes

DEFAULT_FOLDER = royal_kludge/rk68/rgb/ansi