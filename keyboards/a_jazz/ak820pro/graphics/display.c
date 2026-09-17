// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#include "graphics/display.h"

#include "rtc/rtc.h"
#include "media/media.h"
#include "graphics/nowplaying.h"

#include "res/sonixqmk.qgf.h"
#include "res/Iosevka-Regular-30.qff.h"
#include "res/Iosevka-Medium-20.qff.h"

#include "res/apple_icon_24x24.qgf.h"
#include "res/windows_icon_24x24.qgf.h"
#include "res/cable_icon_24x24.qgf.h"
#include "res/bluetooth_icon_24x24.qgf.h"
#include "res/2_4_g_icon_24x24.qgf.h"

#define PANEL_DC        D14
#define PANEL_CS        B8
#define PANEL_RST       A17
#define PANEL_BKL       A16

#define PANEL_WIDTH     128
#define PANEL_HEIGHT    128

#define LCD_OFFSET_X 1
#define LCD_OFFSET_Y 2

// Bottom row: y position of the wireless status line (Iosevka 20 is ~20px
// tall, so 106 leaves it clear of the panel bottom at 128).
#define STATUS_Y 106

// Now-playing / battery-gauge layout (mirror of the lcd-flash backends).
#define GAUGE_Y   27
#define GAUGE_H   6
#define GX0       2
#define GX1       126
#define ZONE_Y0   36
#define NP_TITLE_Y1 40
#define NP_TITLE_Y2 64
#define NP_ARTIST_Y 90
#define NP_BAR_Y    118
#define NP_BAR_H    6
// NP_CHARS / NP_LINE_H come from graphics/nowplaying.h.
// QP colours as HSV triplets (hue 0-255); NP_ prefix avoids QMK's HSV_* macros.
#define NP_BLACK 0, 0, 0
#define NP_TRACK 0, 0, 45       // dark grey (gauge/progress track)
#define NP_BATT  85, 200, 230   // green (battery fill)
#define NP_GREY  0, 0, 150      // dim grey (artist text)

static painter_device_t qp_display;
static painter_font_handle_t qp_font;        // big clock font (Iosevka 30)
static painter_font_handle_t qp_status_font; // small status font (Iosevka 20)
static painter_image_handle_t qp_splash_image;

static painter_image_handle_t qp_mac_logo;
static painter_image_handle_t qp_win_logo;
static painter_image_handle_t qp_usb_logo;
static painter_image_handle_t qp_bt_logo;
static painter_image_handle_t qp_2_4g_logo;

static bool display_powered = true;
static bool splash_cleared = false;
static bool mac_mode = false;
static bool display_paused = false;   // true while asleep: skip the per-second redraw

enum {
    CONN_MODE_WIRED = 0,
    CONN_MODE_BLUETOOTH,
    CONN_MODE_2_4G
};
static uint8_t connection_mode = CONN_MODE_WIRED;

bool display_get_power(void) {
    return display_powered;
}

// Backlight is event-driven: written here whenever the power state changes, so
// housekeeping doesn't need to poll it.
void display_set_power(bool on) {
    display_powered = on;
    gpio_write_pin(PANEL_BKL, on);
}

void display_toggle_power(void) {
    display_set_power(!display_powered);
}

// Low-power display state (idle-sleep / host-suspend). No animation on this
// backend, so just stop the per-second dashboard redraw, blank the panel via QP
// (display-off) and cut the backlight; wake reverses it and forces a full
// repaint. Idempotent via s_slept. Driven by kb_sleep_task in ak820pro.c.
uint32_t display_redraw_dashboard(uint32_t trigger_time, void *cb_arg);   // defined below
static bool s_slept = false;

void display_enter_sleep(void) {
    if (s_slept) return;
    s_slept = true;
    display_paused = true;                 // stop the housekeeping clock redraw
    qp_power(qp_display, false);           // GC9107 display-off (blank)
    display_set_power(false);              // backlight off
}

void display_exit_sleep(void) {
    if (!s_slept) return;
    s_slept = false;
    qp_power(qp_display, true);            // display-on
    display_set_power(true);               // backlight on
    display_paused = false;
    display_redraw_dashboard(0, NULL);     // full repaint
}

static bool display_backlight_init(void) {
    gpio_set_pin_output(PANEL_BKL);
    gpio_write_pin(PANEL_BKL, display_powered); // initial state (on)
    return true;
}

// y position of the big clock (top of the glyphs).
#define CLOCK_Y 49

// Clock format: 1 = HH:MM:SS (per-second redraw of the changed cells), 0 = HH:MM
// (redraws only once a minute -> even cheaper SPI). Override in config.h.
#ifndef DISPLAY_CLOCK_SHOW_SECONDS
#    define DISPLAY_CLOCK_SHOW_SECONDS TRUE
#endif

// Set by display_redraw_dashboard() after it clears the screen, to force a full
// clock+date repaint over the cleared background (the per-cell diff below would
// otherwise skip an unchanged string). Starts true so the first paint is full.
static bool clock_force_repaint = true;

static void draw_status(bool force);       // connection digit + battery gauge
static bool nowplaying_view(void);         // media active and not force-cleared
static void draw_nowplaying(bool force);   // now-playing block (below the gauge)

// Date DD/MM, top-right of the status line -- shown in both views.
static void draw_date(bool force) {
    rtc_time_t shown;
    if (!rtc_get_time(&shown)) return;
    static uint8_t drawn_day = 0xFF, drawn_month = 0xFF;
    if (!force && shown.day == drawn_day && shown.month == drawn_month) return;
    drawn_day = shown.day; drawn_month = shown.month;
    char date_str[8];
    snprintf(date_str, sizeof(date_str), "%02u/%02u", (unsigned)shown.day, (unsigned)shown.month);
    int16_t w = qp_textwidth(qp_status_font, date_str);
    qp_drawtext(qp_display, PANEL_WIDTH - 1 - w, 2, qp_status_font, date_str);
}

// Big HH:MM(:SS) clock, clock view only. Per-cell diff so a tick repaints seconds.
static void draw_clock_time(void) {
    rtc_time_t shown;
    bool valid = rtc_get_time(&shown);
    if (!valid) memset(&shown, 0, sizeof(shown));
    static char last_time[12] = {0};
    if (clock_force_repaint) memset(last_time, 0, sizeof(last_time));
    char time_str[12];
#if DISPLAY_CLOCK_SHOW_SECONDS
    snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u",
             (unsigned)shown.hours, (unsigned)shown.minutes, (unsigned)shown.seconds);
#else
    snprintf(time_str, sizeof(time_str), "%02u:%02u",
             (unsigned)shown.hours, (unsigned)shown.minutes);
#endif
    if (strcmp(time_str, last_time) != 0) {
        uint8_t n       = (uint8_t)strlen(time_str);
        int16_t total_w = qp_textwidth(qp_font, time_str);
        int16_t x0      = (PANEL_WIDTH - total_w) / 2;
        int16_t cw      = total_w / n;
        for (uint8_t i = 0; i < n; i++) {
            if (time_str[i] != last_time[i]) {
                char ch[2] = {time_str[i], 0};
                qp_drawtext(qp_display, x0 + i * cw, CLOCK_Y, qp_font, ch);
            }
        }
        strcpy(last_time, time_str);
    }
    clock_force_repaint = false;
}

uint32_t display_redraw_dashboard(uint32_t trigger_time, void *cb_arg) {

    splash_cleared = true;

    // Clear background
    qp_rect(qp_display, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, 0, 255, 0, true);

    // Full repaint: force the clock and date to redraw over the cleared screen.
    clock_force_repaint = true;

    // Update Mac/Windows icon
    if(mac_mode)
        qp_drawimage(qp_display, 0, 0, qp_mac_logo);
    else
        qp_drawimage(qp_display, 0, 0, qp_win_logo);

    // Update connection icon
    if(connection_mode == CONN_MODE_WIRED)
        qp_drawimage(qp_display, 32, 0, qp_usb_logo);
    else if(connection_mode == CONN_MODE_BLUETOOTH)
        qp_drawimage(qp_display, 32, 0, qp_bt_logo);
    else if(connection_mode == CONN_MODE_2_4G)
        qp_drawimage(qp_display, 32, 0, qp_2_4g_logo);

    // Status line date + connection digit + battery gauge (both views).
    draw_date(true);
    draw_status(true);

    // Content zone: clock or now-playing.
    if (nowplaying_view()) draw_nowplaying(true);
    else                   draw_clock_time();

    // Flush the display to ensure everything is drawn
    qp_flush(qp_display);

    return 0; // Return 0 to stop, or return a time (in ms) to repeat
}

bool display_init_kb(void) {

    qp_display = qp_gc9107_make_spi_device(
        PANEL_WIDTH,
        PANEL_HEIGHT,
        PANEL_CS,
        PANEL_DC,
        PANEL_RST,
        2, //spi_divisor (was 4; 2 = ~2x faster SPI -> shorter blocking flush)
        3  //spi_mode
    );         // Create the display

    qp_set_viewport_offsets(qp_display, LCD_OFFSET_X, LCD_OFFSET_Y);
    qp_init(qp_display, QP_ROTATION_270);   // Initialise the display

    qp_rect(qp_display, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, 0, 255, 0, true);

    qp_font = qp_load_font_mem(font_Iosevka_Regular_30);
    qp_status_font = qp_load_font_mem(font_Iosevka_Medium_20);
    qp_splash_image = qp_load_image_mem(gfx_sonixqmk);

    qp_mac_logo = qp_load_image_mem(gfx_apple_icon_24x24);
    qp_win_logo = qp_load_image_mem(gfx_windows_icon_24x24);
    qp_usb_logo = qp_load_image_mem(gfx_cable_icon_24x24);
    qp_bt_logo  = qp_load_image_mem(gfx_bluetooth_icon_24x24);
    qp_2_4g_logo = qp_load_image_mem(gfx_2_4_g_icon_24x24);

    if(qp_splash_image != NULL)
        qp_drawimage(qp_display, 0, 0, qp_splash_image);

    qp_close_image(qp_splash_image);

    // LCD backlight on
    display_backlight_init();

    bool res = display_init_user();
    if(res) // No more display initialization steps, flush the display to ensure everything is drawn
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
// drawn just to its right; keep the clear cell narrow so it never reaches the
// top-right date.
#define CONN_ICON_X   32
#define CONN_ICON_W   24
#define CONN_NUM_X    (CONN_ICON_X + CONN_ICON_W + 1)
#define CONN_NUM_W    12

// Channel digit next to the connection icon: the connected BT slot (1-3) in BT
// mode, '1' in 2.4G mode, nothing for USB or while not connected. Redrawn only on
// change (or when forced after a dashboard repaint).
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

    // Clear the digit cell (match the green dashboard background), then draw.
    qp_rect(qp_display, CONN_NUM_X, 0, CONN_NUM_X + CONN_NUM_W, CONN_ICON_W - 1, 0, 255, 0, true);
    if (c) {
        char s[2] = {c, 0};
        qp_drawtext(qp_display, CONN_NUM_X, 2, qp_status_font, s);
    }
}

// Bottom row: just the CH582F battery level (right-aligned). Redrawn only when it
// changes, to avoid hammering the slow display SPI on every housekeeping pass.
// Full-width battery gauge below the status line (replaces the old bottom % text),
// shown in both views. Repaints only on a level change (or force).
static void draw_battery_gauge(bool force) {
    static uint8_t last = 0xFE;
    uint8_t b = ch582_get_battery();
    if (b > 100) b = 0;
    if (!force && b == last) return;
    last = b;
    qp_rect(qp_display, GX0, GAUGE_Y, GX1, GAUGE_Y + GAUGE_H - 1, NP_TRACK, true);
    uint16_t fw = (uint16_t)((uint32_t)(GX1 - GX0) * b / 100u);
    if (fw) qp_rect(qp_display, GX0, GAUGE_Y, GX0 + fw, GAUGE_Y + GAUGE_H - 1, NP_BATT, true);
}

// Now-playing block: redraw only the parts the shared model reports as changed.
static void draw_nowplaying(bool force) {
    np_render_t r;
    np_render(&r, force, GX1 - GX0);
    if (!r.changed) return;

    if (r.changed & NP_CH_L1) {
        qp_rect(qp_display, 0, NP_TITLE_Y1, PANEL_WIDTH - 1, NP_TITLE_Y1 + NP_LINE_H - 1, NP_BLACK, true);
        if (r.l1[0]) qp_drawtext(qp_display, 2, NP_TITLE_Y1, qp_status_font, r.l1);
    }
    if (r.changed & NP_CH_L2) {
        qp_rect(qp_display, 0, NP_TITLE_Y2, PANEL_WIDTH - 1, NP_TITLE_Y2 + NP_LINE_H - 1, NP_BLACK, true);
        if (r.l2[0]) qp_drawtext(qp_display, 2, NP_TITLE_Y2, qp_status_font, r.l2);
    }
    if (r.changed & NP_CH_ART) {
        qp_rect(qp_display, 0, NP_ARTIST_Y, PANEL_WIDTH - 1, NP_ARTIST_Y + NP_LINE_H - 1, NP_BLACK, true);
        if (r.art[0]) qp_drawtext_recolor(qp_display, 2, NP_ARTIST_Y, qp_status_font, r.art, NP_GREY, NP_BLACK);
    }
    if (r.changed & NP_CH_BAR) {
        uint8_t ph = r.playing ? 150 : 0;      // blue playing, grey paused
        uint8_t ps = r.playing ? 255 : 0;
        uint8_t pv = r.playing ? 255 : 140;
        if (r.bar_full) {
            qp_rect(qp_display, GX0, NP_BAR_Y, GX1, NP_BAR_Y + NP_BAR_H - 1, NP_TRACK, true);
            if (r.fill) qp_rect(qp_display, GX0, NP_BAR_Y, GX0 + r.fill, NP_BAR_Y + NP_BAR_H - 1, ph, ps, pv, true);
        } else {
            qp_rect(qp_display, GX0 + r.prev_fill, NP_BAR_Y, GX0 + r.fill, NP_BAR_Y + NP_BAR_H - 1, ph, ps, pv, true);
        }
    }
}

// View selection + force-clock toggle (SCR_MEDIA).
static bool s_force_clock = false;
static bool nowplaying_view(void) { return media_show() && !s_force_clock; }

void display_toggle_media(void) {
    s_force_clock = !s_force_clock;
    clock_force_repaint = true;
    display_redraw_dashboard(0, NULL);
}

static void draw_status(bool force) {
    draw_conn_number(force);
    draw_battery_gauge(force);
}

void display_housekeeping_task(void) {
    if (display_paused) return;   // asleep: no redraw (panel is off)

    // Call the user-defined housekeeping task first. If it returns false, skip the default housekeeping.
    if(!display_housekeeping_task_user())
        return;

    if (!splash_cleared) return;

    draw_conn_number(false);      // ~10 Hz for the blink; self-guarded

    // Switch views (media started/stopped/paused-out, or the force-clock key).
    static int8_t last_view = -1;
    int8_t view = nowplaying_view() ? 1 : 0;
    if (view != last_view) {
        last_view = view;
        display_redraw_dashboard(0, NULL);   // already flushes
        return;
    }

    bool drew = false;
    if (view) { draw_nowplaying(false); drew = true; }   // metadata + progress (self-diffs)

    static uint32_t last_sec = UINT32_MAX;
    uint32_t sec = rtc_get_seconds();
    if (sec != last_sec) {
        last_sec = sec;
        draw_date(false);
        draw_battery_gauge(false);
        if (!view) draw_clock_time();        // clock ticks (bar handled above)
        drew = true;
    }

    if (drew) qp_flush(qp_display);
}

void display_draw_mac_logo(void) {
    mac_mode = true;
    if(splash_cleared)
        qp_drawimage(qp_display, 0, 0, qp_mac_logo);
}

void display_draw_windows_logo(void) {
    mac_mode = false;
    if(splash_cleared)
        qp_drawimage(qp_display, 0, 0, qp_win_logo);
}

void display_draw_usb_logo(void) {
    connection_mode = CONN_MODE_WIRED;
    if(splash_cleared)
        qp_drawimage(qp_display, 32, 0, qp_usb_logo);
}

void display_draw_bluetooth_logo(void) {
    connection_mode = CONN_MODE_BLUETOOTH;
    if(splash_cleared)
        qp_drawimage(qp_display, 32, 0, qp_bt_logo);
}

void display_draw_2_4_g_logo(void) {
    connection_mode = CONN_MODE_2_4G;
    if(splash_cleared)
        qp_drawimage(qp_display, 32, 0, qp_2_4g_logo);
}
