// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* RGB matrix configuration can't be fully expressed in JSON as of now */
//#define SN32F2XX_RGB_MATRIX_ROW_PINS { A11, B4, B5, A8, A9, D8, D9, D10, D11, D12, D13, D16, D17, D18, C10, C11, C12, C13 }

/* Configure the effects:                                              */
//#define RGB_MATRIX_TYPING_HEATMAP_DECREASE_DELAY_MS 50

#define SPI_DRIVER SPID0

#define SPI_MOSI_PIN    D2
#define SPI_SCK_PIN     D0
#define SPI_MISO_PIN    NO_PIN
#define SPI_SS_PIN      B8

#define CH582_SERIAL_DRIVER SD2

/* NO_USB_STARTUP_CHECK is enabled automatically by BLUETOOTH_ENABLE (custom
 * driver); it keeps the main loop (matrix scan + key processing) running when
 * USB is suspended/unplugged so wireless typing works on battery. */

/* The mode slider drives the connection host explicitly (dip_switch_update_user),
 * but define a sane boot default before the first slider callback fires. */
#define CONNECTION_HOST_DEFAULT CONNECTION_HOST_USB

#define LED_WINLOCK_PIN     C15
#define LED_CHARGING_PIN    B18

#define CHARGE_CHRG_PIN   B16
#define CHARGE_STDBY_PIN  B17


#define CORTEX_ENABLE_WFI_IDLE FALSE

#define QUANTUM_PAINTER_SUPPORTS_NATIVE_COLORS TRUE
#define QUANTUM_PAINTER_SUPPORTS_256_PALETTE TRUE
#define QUANTUM_PAINTER_DISPLAY_TIMEOUT 0
// Persist a small keyboard-specific config block in EEPROM. On SN32F290 QMK's
// default "vendor" EEPROM driver is wear-leveling backed by MCU internal flash.
// Currently: the last Bluetooth slot, so it survives power cycles and mode
// switches. Bump EECONFIG_KB_DATA_VERSION if the layout changes.
#define EECONFIG_KB_DATA_SIZE    4
#define EECONFIG_KB_DATA_VERSION 1
