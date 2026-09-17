// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// [QP COEXISTENCE BRANCH] Dashboard rendered by Quantum Painter through our bare-metal
// SPI0 bus (graphics/lcd_bus.c), coexisting with the interrupt-driven flash DMA.

#include "graphics/display.h"
#include "lcd_bus.h"

#include <string.h>
#include <stdio.h>
#include "gpio.h"
#include "rtc/rtc.h"
#include "media/media.h"

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

// Now-playing / battery-gauge layout (mirror of the custom backend).
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
#define NP_CHARS    12
// QP colours as HSV triplets (hue 0-255). val=0 is black regardless of hue/sat.
// NP_ prefix avoids QMK's own HSV_* color-constant macros (color.h).
#define NP_BLACK 0, 0, 0
#define NP_TRACK 0, 0, 45       // dark grey (gauge/progress track)
#define NP_BATT  85, 200, 230   // green (battery fill)
#define NP_PROG  150, 255, 255  // accent blue (progress fill)
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

// Backlight is event-driven: written here whenever the power state changes, so
// housekeeping doesn't need to poll it.
void display_set_power(bool on) {
    display_powered = on;
    gpio_write_pin(PANEL_BKL, on);
}

void display_toggle_power(void) {
    display_set_power(!display_powered);
}


// Low-power display state (host-suspend + idle-sleep), the QP-backend twin of the
// custom backend's display_enter/exit_sleep. Same primitive, but the panel is
// blanked through QP (display-off opcode) instead of a raw sleep-in, since QP
// owns SPI0; qp_power() brackets its own comms so this is safe while paused.
// Idempotent via s_slept. anim_toggle() already drains any in-flight DMA frame.
static bool s_slept = false;
static bool s_anim_was_active = false;     // resume the animation on wake if it was playing

void display_enter_sleep(void) {
    if (s_slept) return;
    s_slept = true;
    s_anim_was_active = anim_active();
    if (s_anim_was_active) anim_toggle();  // stop the player (drains in-flight frame)
    display_set_paused(true);              // stop the 10 Hz dashboard repaint
    qp_power(qp_display, false);           // GC9107 display-off (blank)
    display_set_power(false);              // backlight off
}

void display_exit_sleep(void) {
    if (!s_slept) return;
    s_slept = false;
    qp_power(qp_display, true);            // display-on
    display_set_power(true);               // backlight on
    if (s_anim_was_active) anim_toggle();  // resume the animation it was playing
    else display_set_paused(false);        // otherwise unpause -> full dashboard repaint
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

    // GC9107 via the STOCK Quantum Painter SPI driver (ChibiOS SPI0). The flash->LCD
    // DMA coexists by borrowing SPI0 through the patched LLD's Vector58 tunnel hook.
    qp_display = qp_gc9107_make_spi_device(
        PANEL_WIDTH, PANEL_HEIGHT,
        PANEL_CS, PANEL_DC, PANEL_RST,
        2,   // spi_divisor
        3    // spi_mode
    );

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

// Greedy word-wrap into two <=NP_CHARS lines; "..." on the last line if truncated.
static void wrap_title(const char *s, char l1[NP_CHARS + 1], char l2[NP_CHARS + 1]) {
    char *out[2] = { l1, l2 };
    out[0][0] = out[1][0] = 0;
    uint8_t li = 0;
    const char *p = s;
    bool overflow = false;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *w = p;
        while (*p && *p != ' ') p++;
        uint8_t wl = (uint8_t)(p - w);
        uint8_t cl = (uint8_t)strlen(out[li]);
        uint8_t need = cl ? (uint8_t)(cl + 1 + wl) : wl;
        if (need <= NP_CHARS) {
            if (cl) { out[li][cl] = ' '; cl++; }
            memcpy(out[li] + cl, w, wl); out[li][cl + wl] = 0;
        } else if (cl == 0) {
            memcpy(out[li], w, NP_CHARS); out[li][NP_CHARS] = 0;
            overflow = true; break;
        } else if (li == 0) {
            li = 1;
            uint8_t n = wl < NP_CHARS ? wl : NP_CHARS;
            memcpy(out[1], w, n); out[1][n] = 0;
        } else {
            overflow = true; break;
        }
    }
    while (*p == ' ') p++;
    if (overflow || *p) {
        char *L = out[1][0] ? l2 : l1;
        uint8_t n = (uint8_t)strlen(L);
        if (n > NP_CHARS - 3) n = NP_CHARS - 3;
        L[n] = 0; strcat(L, "...");
    }
}

static void fit_line(const char *s, char out[NP_CHARS + 1]) {
    uint8_t n = 0;
    while (s[n] && n < NP_CHARS) { out[n] = s[n]; n++; }
    out[n] = 0;
    if (s[n]) { if (n > NP_CHARS - 3) n = NP_CHARS - 3; out[n] = 0; strcat(out, "..."); }
}

// Progress bar: a tick extends the fill by the new strip only (time moves forward);
// force / backward jump repaints the whole track+fill once.
static void draw_np_progress(bool force) {
    static uint16_t last_fw = 0xFFFF;
    uint32_t dur = media_duration_ms(), el = media_elapsed_ms();
    uint16_t fw = dur ? (uint16_t)((uint32_t)(GX1 - GX0) * el / dur) : 0;
    if (fw > (GX1 - GX0)) fw = GX1 - GX0;
    if (!force && fw == last_fw) return;
    if (force || fw < last_fw) {
        qp_rect(qp_display, GX0, NP_BAR_Y, GX1, NP_BAR_Y + NP_BAR_H - 1, NP_TRACK, true);
        if (fw) qp_rect(qp_display, GX0, NP_BAR_Y, GX0 + fw, NP_BAR_Y + NP_BAR_H - 1, NP_PROG, true);
    } else {
        qp_rect(qp_display, GX0 + last_fw, NP_BAR_Y, GX0 + fw, NP_BAR_Y + NP_BAR_H - 1, NP_PROG, true);
    }
    last_fw = fw;
}

// Full now-playing block: title (2 wrapped lines, white) + artist (grey) + bar.
static void draw_nowplaying(bool force) {
    if (!force && !media_take_dirty()) return;
    qp_rect(qp_display, 0, ZONE_Y0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1, NP_BLACK, true);

    char l1[NP_CHARS + 1], l2[NP_CHARS + 1];
    wrap_title(media_title(), l1, l2);
    if (l1[0]) qp_drawtext(qp_display, 2, NP_TITLE_Y1, qp_status_font, l1);
    if (l2[0]) qp_drawtext(qp_display, 2, NP_TITLE_Y2, qp_status_font, l2);

    char art[NP_CHARS + 1];
    fit_line(media_artist(), art);
    if (art[0]) qp_drawtext_recolor(qp_display, 2, NP_ARTIST_Y, qp_status_font, art, NP_GREY, NP_BLACK);

    draw_np_progress(true);
}

// View selection + force-clock toggle (SCR_MEDIA).
static bool s_force_clock = false;
static bool nowplaying_view(void) { return media_active() && !s_force_clock; }

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
    // Call the user-defined housekeeping task first. If it returns false, skip the default housekeeping.
    if(!display_housekeeping_task_user())
        return;

    if (display_paused) return;   // animation owns the bus
    if (!splash_cleared) return;

    draw_conn_number(false);      // ~10 Hz for the blink; self-guarded

    // Switch views (media started/stopped, or the force-clock key) -> full repaint.
    static int8_t last_view = -1;
    int8_t view = nowplaying_view() ? 1 : 0;
    if (view != last_view) {
        last_view = view;
        display_redraw_dashboard(0, NULL);   // already flushes
        return;
    }

    bool drew = false;
    if (view) { draw_nowplaying(false); drew = true; }   // metadata change (self-guarded)

    static uint32_t last_sec = UINT32_MAX;
    uint32_t sec = rtc_get_seconds();
    if (sec != last_sec) {
        last_sec = sec;
        draw_date(false);
        draw_battery_gauge(false);
        if (view) draw_np_progress(false);   // extend the fill by the new strip
        else      draw_clock_time();         // clock ticks
        drew = true;
    }

    if (drew) qp_flush(qp_display);
}

void display_draw_mac_logo(void) {
    mac_mode = true;
    if(splash_cleared && !display_paused)
        qp_drawimage(qp_display, 0, 0, qp_mac_logo);
}

void display_draw_windows_logo(void) {
    mac_mode = false;
    if(splash_cleared && !display_paused)
        qp_drawimage(qp_display, 0, 0, qp_win_logo);
}

void display_draw_usb_logo(void) {
    connection_mode = CONN_MODE_WIRED;
    if(splash_cleared && !display_paused)
        qp_drawimage(qp_display, 32, 0, qp_usb_logo);
}

void display_draw_bluetooth_logo(void) {
    connection_mode = CONN_MODE_BLUETOOTH;
    if(splash_cleared && !display_paused)
        qp_drawimage(qp_display, 32, 0, qp_bt_logo);
}

void display_draw_2_4_g_logo(void) {
    connection_mode = CONN_MODE_2_4G;
    if(splash_cleared && !display_paused)
        qp_drawimage(qp_display, 32, 0, qp_2_4g_logo);
}
