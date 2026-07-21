// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* RGB matrix (software PWM on SN32F299). The 18 hardware row pins are 6 key-rows
 * x 3 colour channels in R,B,G order per group. Columns are shared with the key
 * matrix (COL_PINS omitted -> SHARED_MATRIX uses MATRIX_COL_PINS). */
#define SN32F2XX_RGB_MATRIX_ROW_PINS { A11, B4, B5, A8, A9, D8, D9, D10, D11, D12, D13, D16, D17, D18, C10, C11, C12, C13 }
#define SN32F2XX_PWM_CONTROL   SOFTWARE_PWM
#define SN32F2XX_PWM_DIRECTION COL2ROW      // PWM (software) on columns, rows are the mux select
#define SN32F2XX_RGB_MATRIX_ROW_CHANNELS 3  // R, B, G
// ROWS defaults to MATRIX_ROWS (6); ROWS_HW = ROWS * ROW_CHANNELS = 18; COLS = MATRIX_COLS (15).

// The per-key LED map (81 LEDs; knob at matrix [0][14] has none) and
// RGB_MATRIX_LED_COUNT are defined by the rgb_matrix "layout" in keyboard.json.

// Columns are shared between the key matrix and the (column-active-LOW) LED matrix.
// Drive unselected key-rows HIGH (instead of leaving them high-Z): a pressed switch
// then pulls its shared column to INACTIVE (high) rather than active (low), so a
// keypress no longer lights up its whole LED column ("column leak").
#define MATRIX_UNSELECT_DRIVE_HIGH

// Software PWM tuning. The SN32 SW-PWM ISR re-arms itself continuously, so its CPU
// cost = callback_rate x per_callback_cost, competing with USB / the LCD SPI / the
// wireless UART. Two knobs balance flicker vs CPU headroom:
//   - HUE_STEP sizes the per-callback PWM loop (COLS x HUE_STEP GPIO ops). Lower =
//     cheaper ISR and (perceptually) less flicker; 2 tested smoothest here.
//   - LED_PROCESS_LIMIT sizes the callback rate; 17 = the natural value for 81 LEDs.
// (Clean typing ultimately came from cutting the LCD's per-second SPI flush cost,
// not from these -- see graphics/display.c.)
#define RGB_MATRIX_HUE_STEP 2
#define RGB_MATRIX_LED_PROCESS_LIMIT 17

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
