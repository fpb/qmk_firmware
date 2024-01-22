# Data-driven structure doesn't support EEPROM yet
EEPROM_DRIVER = wear_leveling
WEAR_LEVELING_DRIVER = sn32_flash

KEYBOARD_SHARED_EP 		= yes

BLUETOOTH_ENABLE 		= yes
BLUETOOTH_DRIVER		= iton_bt

RGB_MATRIX_CUSTOM_USER = yes

SRC = rk61.c
