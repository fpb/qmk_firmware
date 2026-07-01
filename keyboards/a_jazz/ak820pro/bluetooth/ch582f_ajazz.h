#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "report.h"

typedef enum {
    CH582_PROFILE_PEER_24G = 0x30, /* '0' */
    CH582_PROFILE_BT_1     = 0x31, /* '1' */
    CH582_PROFILE_BT_2     = 0x32, /* '2' */
    CH582_PROFILE_BT_3     = 0x33, /* '3' */
    CH582_PROFILE_PAIR_24G = 0x35  /* '5' */
} ch582_profile_t;

/* Init/task are provided through QMK's Bluetooth driver API (bluetooth_init /
 * bluetooth_task in ch582f_ajazz.c); ch582_task() is the internal RX/poll pump
 * called from bluetooth_task(). */
void ch582_task(void);
void ch582_set_profile(ch582_profile_t profile);
void ch582_pair(ch582_profile_t profile);
/* Put the CURRENTLY-SELECTED slot into pairing (stock `A6 51`, slotless). Call
 * ch582_set_profile() first to choose the slot. */
void ch582_enter_pairing(void);
void ch582_cancel_connect(void);
void ch582_poll_status(void);
void ch582_send_command(uint8_t cmd, const uint8_t *params, uint8_t param_len);
void ch582_send_keyboard_report(report_keyboard_t *report);
bool ch582_is_connected(void);
bool ch582_is_pairing(void);
/* True when the selected profile is the 2.4GHz dongle (not a BT slot). */
bool ch582_is_24g(void);
/* True when the mode slider is in the USB position (wireless not in use). */
bool ch582_is_usb(void);
uint8_t ch582_get_slot(void);

/* Battery level reported by the module (0-100, 0xFF = unknown). From 5C frames. */
uint8_t ch582_get_battery(void);
/* Host LED bitmap last reported by the module (USB LED bits; bit1 = caps). From 5A frames. */
uint8_t ch582_get_host_leds(void);
