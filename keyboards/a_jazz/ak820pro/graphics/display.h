// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#include "qp.h"
#include "rtc/rtc.h"

bool display_init_kb(void);
bool display_init_user(void);
void display_housekeeping_task(void);

void display_set_power(bool on);
bool display_get_power(void);
void display_toggle_power(void);

// Enter/exit the low-power display state (panel display-off + backlight off,
// redraw paused). Idempotent. Driven by the idle timer / USB-suspend poll.
void display_enter_sleep(void);
void display_exit_sleep(void);

void display_draw_mac_logo(void);
void display_draw_windows_logo(void);
void display_draw_usb_logo(void);
void display_draw_bluetooth_logo(void);
void display_draw_2_4_g_logo(void);
