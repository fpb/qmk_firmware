// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdint.h>
#include <stdbool.h>
#include "rtc/rtc.h"

bool display_init_kb(void);
bool display_init_user(void);
void display_housekeeping_task(void);

void display_set_power(bool on);
bool display_get_power(void);
void display_toggle_power(void);

// Enter/exit the low-power display state (panel sleep-in + backlight off, repaint
// paused). Idempotent. Driven by host-suspend now; reused by the idle-sleep timer.
void display_enter_sleep(void);
void display_exit_sleep(void);

void display_draw_mac_logo(void);
void display_draw_windows_logo(void);
void display_draw_usb_logo(void);
void display_draw_bluetooth_logo(void);
void display_draw_2_4_g_logo(void);

// General blink phase for the LCD: true during the first half of each period_ms
// cycle (period_ms==0 => always on). Gate any drawn element on it to make it blink.
bool display_blink(uint16_t period_ms);
