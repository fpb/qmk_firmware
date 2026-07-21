// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bare-metal dashboard: renders directly through graphics/lcd_bus.c (no Quantum
// Painter). QP's asset blobs (qgf images, qff fonts) are decoded by lcd_bus.

#include "graphics/display.h"
#include "lcd_bus.h"

#include <string.h>
#include <stdio.h>
#include "quantum.h"
#include "gpio.h"
#include "rtc/rtc.h"



#define PANEL_BKL       A16

#define PANEL_WIDTH     128
#define PANEL_HEIGHT    128

#define COL_BG          0x0000   // black background
#define COL_FG          0xFFFF   // white text

// Font blobs (Iosevka, mono 1bpp).
// Fonts and images now live in external flash and are DMA-drawn; these are
// ids into the index that flash_assets_init() reads at boot. The full 95-glyph
// atlases are used, so EMBED_CHARSET and its "letters silently draw nothing"
// trap are gone.
#define FONT_CLOCK      ASSET_IOSEVKA_REGULAR_30   // big clock
#define FONT_STATUS     ASSET_IOSEVKA_MEDIUM_20    // small status text

// Bottom row: y position of the wireless status line.
#define STATUS_Y 106

static bool display_powered = true;
static bool display_paused  = false;   // true while the flash-animation player owns the bus
static bool splash_cleared = false;
static bool mac_mode = false;

uint32_t display_redraw_dashboard(uint32_t trigger_time, void *cb_arg);
void display_set_paused(bool paused) {
    display_paused = paused;
    if (!paused) display_redraw_dashboard(0, NULL);   // resume: full repaint
}

enum {
    CONN_MODE_WIRED = 0,
    CONN_MODE_BLUETOOTH,
    CONN_MODE_2_4G
};
static uint8_t connection_mode = CONN_MODE_WIRED;

bool display_get_power(void) {
    return display_powered;
}

// Backlight is event-driven: written here whenever the power state changes.
void display_set_power(bool on) {
    display_powered = on;
    gpio_write_pin(PANEL_BKL, on);
}

void display_toggle_power(void) {
    display_set_power(!display_powered);
}

static bool display_backlight_init(void) {
    gpio_set_pin_output(PANEL_BKL);
    gpio_write_pin(PANEL_BKL, display_powered); // initial state (on)
    return true;
}

// y position of the big clock (top of the glyphs).
#define CLOCK_Y 49

// Clock format: 1 = HH:MM:SS (per-second redraw of the changed cells), 0 = HH:MM.
#ifndef DISPLAY_CLOCK_SHOW_SECONDS
#    define DISPLAY_CLOCK_SHOW_SECONDS TRUE
#endif

// Forces a full clock+date repaint after the background is cleared.
static bool clock_force_repaint = true;

static void draw_status(bool force); // CH582F status: battery + channel digit

void draw_clock(void) {
    rtc_time_t shown;
    bool valid = rtc_get_time(&shown);
    if (!valid) memset(&shown, 0, sizeof(shown));

    // Time HH:MM:SS -- redraw only the character cells that changed (usually just the
    // seconds). The clock font is monospace, so every cell has the same width.
    static char last_time[12] = {0};
    if (clock_force_repaint) memset(last_time, 0, sizeof(last_time)); // invalidate -> full repaint
    char time_str[12];
#if DISPLAY_CLOCK_SHOW_SECONDS
    snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u",
             (unsigned)shown.hours, (unsigned)shown.minutes, (unsigned)shown.seconds);
#else
    snprintf(time_str, sizeof(time_str), "%02u:%02u",
             (unsigned)shown.hours, (unsigned)shown.minutes);
#endif
    if (strcmp(time_str, last_time) != 0) {
        uint8_t  n       = (uint8_t)strlen(time_str);        // 8 (HH:MM:SS) or 5 (HH:MM)
        uint16_t total_w = lcd_flash_text_width(FONT_CLOCK, time_str);
        int16_t  x0      = (PANEL_WIDTH - total_w) / 2;
        int16_t  cw      = total_w / n;                      // monospace cell width
        for (uint8_t i = 0; i < n; i++) {
            if (time_str[i] != last_time[i]) {
                int16_t cx = x0 + i * cw;
                char ch[2] = {time_str[i], 0};
                lcd_draw_flash_text(FONT_CLOCK, cx, CLOCK_Y, ch);
            }
        }
        strcpy(last_time, time_str);
    }
    // Date DD/MM, top-right. Repainted on a forced full paint or when it changes.
    static uint8_t drawn_day = 0, drawn_month = 0;
    if (valid && (clock_force_repaint || shown.day != drawn_day || shown.month != drawn_month)) {
        drawn_day   = shown.day;
        drawn_month = shown.month;
        char date_str[8];
        snprintf(date_str, sizeof(date_str), "%02u/%02u",
                 (unsigned)shown.day, (unsigned)shown.month);
        uint16_t w = lcd_flash_text_width(FONT_STATUS, date_str);
        lcd_draw_flash_text(FONT_STATUS, PANEL_WIDTH - 1 - w, 2, date_str);
    }

    clock_force_repaint = false; // consumed by both the time and date above
}

uint32_t display_redraw_dashboard(uint32_t trigger_time, void *cb_arg) {
    splash_cleared = true;

    // Clear background.
    lcd_fill_rect(0, 0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1, COL_BG);

    // Full repaint: force the clock and date to redraw over the cleared screen.
    clock_force_repaint = true;

    // Mac/Windows icon (top-left).
    lcd_draw_flash_image(mac_mode ? ASSET_APPLE_ICON_24X24 : ASSET_WINDOWS_ICON_24X24, 0, 0);

    // Connection icon.
    if (connection_mode == CONN_MODE_WIRED)          lcd_draw_flash_image(ASSET_CABLE_ICON_24X24, 32, 0);
    else if (connection_mode == CONN_MODE_BLUETOOTH) lcd_draw_flash_image(ASSET_BLUETOOTH_ICON_24X24, 32, 0);
    else if (connection_mode == CONN_MODE_2_4G)      lcd_draw_flash_image(ASSET_2_4_G_ICON_24X24, 32, 0);

    draw_clock();
    draw_status(true);   // battery + channel digit over the cleared screen

    return 0; // one-shot
}

bool display_init_kb(void) {
    lcd_init();          // GC9107 bring-up, rotation 270

    // Splash logo, held until the deferred dashboard repaint below.
    lcd_fill_rect(0, 0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1, COL_BG);
    // All art lives in external flash now, so nothing can be drawn until the
    // index is read. An unprovisioned keyboard therefore shows a BLANK panel and
    // says so on the console -- there is no embedded fallback left to draw with,
    // which is the whole point (it reclaimed ~32KB of firmware). Provision with:
    //   ak820ctl flash write 0x0CE0000 graphics/res/flash_assets.bin
    if (flash_assets_init()) {
        dprintf("[assets] index ok, %u entries\n", flash_assets_count());
        lcd_draw_flash_image(ASSET_SONIXQMK, 0, 0);
    } else {
        dprintf("[assets] NO VALID INDEX at 0x%06lX -- panel stays blank.\n"
                "[assets] provision with: ak820ctl flash write 0x%06lX flash_assets.bin\n",
                (unsigned long)FLASH_ASSET_BASE, (unsigned long)FLASH_ASSET_BASE);
    }

    display_backlight_init();

    bool res = display_init_user();
    if (res)
        defer_exec(1500, display_redraw_dashboard, NULL);

    return true;
}

__attribute__((weak)) bool display_init_user(void) {
    return true;
}

__attribute__((weak)) bool display_housekeeping_task_user(void) {
    return true;
}

extern bool    ch582_is_connected(void);
extern bool    ch582_is_24g(void);
extern bool    ch582_is_usb(void);
extern uint8_t ch582_get_slot(void);
extern uint8_t ch582_get_battery(void);

// The top connection icon lives at (CONN_ICON_X, 0), 24x24. The channel digit is
// drawn just to its right.
#define CONN_ICON_X   32
#define CONN_ICON_W   24
#define CONN_NUM_X    (CONN_ICON_X + CONN_ICON_W + 1)
#define CONN_NUM_W    12

// Channel digit next to the connection icon: connected BT slot (1-3) in BT mode,
// '1' in 2.4G mode, nothing for USB or while not connected.
static void draw_conn_number(bool force) {
    char c = 0; // 0 -> nothing shown next to the icon
    if (ch582_is_connected() && !ch582_is_usb()) {
        if (ch582_is_24g()) {
            c = '1';
        } else {
            uint8_t slot = ch582_get_slot();
            if (slot >= 1 && slot <= 3) c = (char)('0' + slot);
        }
    }

    static char last_c = -1; // force the first paint
    if (!force && c == last_c) return;
    last_c = c;

    // Clear the digit cell, then draw.
    lcd_fill_rect(CONN_NUM_X, 0, CONN_NUM_X + CONN_NUM_W, CONN_ICON_W - 1, COL_BG);
    if (c) {
        char s[2] = {c, 0};
        lcd_draw_flash_text(FONT_STATUS, CONN_NUM_X, 2, s);
    }
}

// Bottom row: the CH582F battery level (right-aligned).
static void draw_battery(bool force) {
    static uint8_t last_batt = 0xFE;
    uint8_t batt = ch582_get_battery();
    if (!force && batt == last_batt) return;
    last_batt = batt;

    // Clear the bottom strip.
    lcd_fill_rect(0, STATUS_Y, PANEL_WIDTH - 1, PANEL_HEIGHT - 1, COL_BG);

    if (batt <= 100) {
        char bbuf[8];
        snprintf(bbuf, sizeof(bbuf), "%u%%", batt);
        uint16_t w = lcd_flash_text_width(FONT_STATUS, bbuf);
        lcd_draw_flash_text(FONT_STATUS, PANEL_WIDTH - 1 - w, STATUS_Y, bbuf);
    }
}

static void draw_status(bool force) {
    draw_conn_number(force);
    draw_battery(force);
}

void display_housekeeping_task(void) {
    if (!display_housekeeping_task_user())
        return;

    if (display_paused) return;   // animation owns the bus

    if (splash_cleared) {
        // Repaint the clock once per RTC second.
        static uint32_t last_shown_sec = UINT32_MAX;
        uint32_t sec = rtc_get_seconds();
        if (sec != last_shown_sec) {
            last_shown_sec = sec;
            draw_clock();
            draw_status(false); // self-guards, only draws on change
        }
    }
}

void display_draw_mac_logo(void) {
    mac_mode = true;
    if (splash_cleared && !display_paused)
        lcd_draw_flash_image(ASSET_APPLE_ICON_24X24, 0, 0);
}

void display_draw_windows_logo(void) {
    mac_mode = false;
    if (splash_cleared && !display_paused)
        lcd_draw_flash_image(ASSET_WINDOWS_ICON_24X24, 0, 0);
}

void display_draw_usb_logo(void) {
    connection_mode = CONN_MODE_WIRED;
    if (splash_cleared && !display_paused)
        lcd_draw_flash_image(ASSET_CABLE_ICON_24X24, 32, 0);
}

void display_draw_bluetooth_logo(void) {
    connection_mode = CONN_MODE_BLUETOOTH;
    if (splash_cleared && !display_paused)
        lcd_draw_flash_image(ASSET_BLUETOOTH_ICON_24X24, 32, 0);
}

void display_draw_2_4_g_logo(void) {
    connection_mode = CONN_MODE_2_4G;
    if (splash_cleared && !display_paused)
        lcd_draw_flash_image(ASSET_2_4_G_ICON_24X24, 32, 0);
}
