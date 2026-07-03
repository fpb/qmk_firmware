// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#include "graphics/display.h"

#include "rtc/rtc.h"

#include "res/sonixqmk.qgf.h"
#include "res/Iosevka-Regular-30.qff.h"
#include "res/robotomono20.qff.h"

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

// Bottom row: y position of the wireless status line (Roboto Mono 20 is ~20px
// tall, so 106 leaves it clear of the panel bottom at 128).
#define STATUS_Y 106

static painter_device_t qp_display;
static painter_font_handle_t qp_font;        // big clock font (Iosevka 30)
static painter_font_handle_t qp_status_font; // small status font (Roboto Mono 20)
static painter_image_handle_t qp_splash_image;

static painter_image_handle_t qp_mac_logo;
static painter_image_handle_t qp_win_logo;
static painter_image_handle_t qp_usb_logo;
static painter_image_handle_t qp_bt_logo;
static painter_image_handle_t qp_2_4g_logo;

static bool display_powered = true;
static bool splash_cleared = false;
static bool mac_mode = false;

enum {
    CONN_MODE_WIRED = 0,
    CONN_MODE_BLUETOOTH,
    CONN_MODE_2_4G
};
static uint8_t connection_mode = CONN_MODE_WIRED;

void display_control_power(void) {
    static bool last_power = false;

    if(display_powered != last_power) {
        gpio_write_pin(PANEL_BKL, display_powered);
        last_power = display_powered;
    }
}

bool display_get_power(void) {
    return display_powered;
}

void display_set_power(bool on) {
    display_powered = on;
}

void display_toggle_power(void) {
    display_powered = !display_powered;
}

static bool display_backlight_init(void) {
    gpio_set_pin_output(PANEL_BKL);
    return true;
}

// y position of the big clock (top of the glyphs).
#define CLOCK_Y 49

// Clock format: 1 = HH:MM:SS (per-second redraw of the changed cells), 0 = HH:MM
// (redraws only once a minute -> even cheaper SPI). Override in config.h.
#ifndef DISPLAY_CLOCK_SHOW_SECONDS
#    define DISPLAY_CLOCK_SHOW_SECONDS 1
#endif

// Force-redraw markers; reset on a full dashboard repaint and on a clock_set.
static uint8_t last_drawn_second = 60; // 60 forces an immediate draw
static bool    date_drawn        = false;

// The software clock: a full date+time captured at base_tick, advanced purely
// by the MCU timer (no periodic RTC resync -- bit-banged I2C would add keystroke
// latency). base rolls forward across midnight so the date stays correct.
static bool       have_base = false;
static rtc_time_t base;            // date+time as of base_tick
static uint32_t   base_tick = 0;   // timer_read32() captured at the seed/commit
static uint32_t   last_try  = 0;   // throttles the boot seed retries

// --- Date arithmetic (for midnight rollover). -------------------------------
static uint8_t days_in_month(uint8_t month, uint16_t year) {
    static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 31;
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return dim[month - 1];
}

static void date_add_days(rtc_time_t *t, uint32_t days) {
    while (days--) {
        if (++t->day > days_in_month(t->month, t->year)) {
            t->day = 1;
            if (++t->month > 12) { t->month = 1; t->year++; }
        }
        if (++t->weekday > 6) t->weekday = 0;
    }
}

// Compute the current displayed time, advancing base across midnight in place.
static void clock_current(rtc_time_t *out) {
    uint32_t now = timer_read32();
    if (!have_base) {
        uint32_t s = now / 1000; // uptime fallback until the first good read
        out->seconds = s % 60;
        out->minutes = (s / 60) % 60;
        out->hours   = (s / 3600) % 24;
        out->day = out->month = out->weekday = 0;
        out->year = 0;
        return;
    }
    uint32_t sod = (uint32_t)base.hours * 3600 + base.minutes * 60 + base.seconds
                 + (now - base_tick) / 1000;
    if (sod >= 86400) { // crossed one or more midnights: roll the date forward
        uint32_t days = sod / 86400;
        date_add_days(&base, days);
        base_tick += days * 86400000UL;
        sod %= 86400;
        base.hours   = (uint8_t)(sod / 3600);
        base.minutes = (uint8_t)((sod / 60) % 60);
        base.seconds = (uint8_t)(sod % 60);
    }
    *out = base;
    out->hours   = (uint8_t)(sod / 3600);
    out->minutes = (uint8_t)((sod / 60) % 60);
    out->seconds = (uint8_t)(sod % 60);
}

// Write-through: re-seed the live software clock from t (e.g. after a host sets
// the RTC over raw HID), so the display jumps to the new time without a reboot.
void display_clock_set(const rtc_time_t *t) {
    base              = *t;
    base_tick         = timer_read32();
    have_base         = true;
    last_drawn_second = 60;   // force the time/date to repaint at the new value
    date_drawn        = false;
}

void draw_clock(void) {
    uint32_t now = timer_read32();

    // Seed the clock from the external PCF8563 RTC ONCE at boot, then free-run on
    // the MCU timer. After one good read we never touch the bus again; until then
    // retry at most once a second (not every housekeeping pass), showing uptime.
    if (!have_base &&
        (last_try == 0 || timer_elapsed32(last_try) >= 1000)) {
        last_try = now ? now : 1; // avoid the "0 == never tried" sentinel
        rtc_time_t t;
        // Accept whatever the chip returns (its VL flag is set since it was never
        // properly set, yet it keeps running time -- like the stock firmware).
        if (rtc_read_raw(&t)) {
            base      = t;
            base_tick = now;
            have_base = true;
        }
    }

    rtc_time_t shown;
    clock_current(&shown);

    // Time HH:MM:SS. To minimise the blocking SPI flush, redraw ONLY the character
    // cells that changed (usually just the seconds) rather than the whole string.
    // The clock font (Iosevka) is monospace, so every cell has the same width.
    static char last_time[12] = {0};
    if (last_drawn_second == 60) memset(last_time, 0, sizeof(last_time)); // forced full repaint
    char time_str[12];
#if DISPLAY_CLOCK_SHOW_SECONDS
    snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u",
             (unsigned)shown.hours, (unsigned)shown.minutes, (unsigned)shown.seconds);
#else
    snprintf(time_str, sizeof(time_str), "%02u:%02u",
             (unsigned)shown.hours, (unsigned)shown.minutes);
#endif
    if (strcmp(time_str, last_time) != 0) {
        uint8_t n       = (uint8_t)strlen(time_str);         // 8 (HH:MM:SS) or 5 (HH:MM)
        int16_t total_w = qp_textwidth(qp_font, time_str);
        int16_t x0      = (PANEL_WIDTH - total_w) / 2;
        int16_t cw      = total_w / n;                       // monospace cell width
        //int16_t fh      = qp_font->line_height;
        for (uint8_t i = 0; i < n; i++) {
            if (time_str[i] != last_time[i]) {
                int16_t cx = x0 + i * cw;
                //qp_rect(qp_display, cx, CLOCK_Y, cx + cw - 1, CLOCK_Y + fh - 1, 0, 255, 0, true); // clear cell (bg)
                char ch[2] = {time_str[i], 0};
                qp_drawtext(qp_display, cx, CLOCK_Y, qp_font, ch);
            }
        }
        strcpy(last_time, time_str);
    }
    last_drawn_second = shown.seconds; // clear the forced-repaint sentinel

    // Date DD/MM, top-right. Repainted once per dashboard paint (date changes
    // only at midnight, where date_drawn is cleared to trigger a redraw).
    static uint8_t drawn_day = 0, drawn_month = 0;
    if (have_base && (!date_drawn || shown.day != drawn_day || shown.month != drawn_month)) {
        date_drawn  = true;
        drawn_day   = shown.day;
        drawn_month = shown.month;
        char date_str[8];
        snprintf(date_str, sizeof(date_str), "%02u/%02u",
                 (unsigned)shown.day, (unsigned)shown.month);
        int16_t w = qp_textwidth(qp_status_font, date_str);
        qp_drawtext(qp_display, PANEL_WIDTH - 1 - w, 2, qp_status_font, date_str);
    }
}

uint32_t display_redraw_dashboard(uint32_t trigger_time, void *cb_arg) {

    splash_cleared = true;

    // Clear background
    qp_rect(qp_display, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, 0, 255, 0, true);

    // Full repaint: force the clock and date to redraw over the cleared screen.
    last_drawn_second = 60;
    date_drawn        = false;

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

    // Draw the clock
    draw_clock();

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

    // LCD backlight on
    display_backlight_init();

    qp_rect(qp_display, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, 0, 255, 0, true);

    qp_font = qp_load_font_mem(font_Iosevka_Regular_30);
    qp_status_font = qp_load_font_mem(font_robotomono20);
    qp_splash_image = qp_load_image_mem(gfx_sonixqmk);

    qp_mac_logo = qp_load_image_mem(gfx_apple_icon_24x24);
    qp_win_logo = qp_load_image_mem(gfx_windows_icon_24x24);
    qp_usb_logo = qp_load_image_mem(gfx_cable_icon_24x24);
    qp_bt_logo  = qp_load_image_mem(gfx_bluetooth_icon_24x24);
    qp_2_4g_logo = qp_load_image_mem(gfx_2_4_g_icon_24x24);

    if(qp_splash_image != NULL)
        qp_drawimage(qp_display, 0, 0, qp_splash_image);

    qp_close_image(qp_splash_image);


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

// Bottom row: wireless mode label (USB / 2.4G / BTn / idle) on the left and the
// CH582F battery level on the right. Redrawn only when something changes, to
// avoid hammering the slow display SPI on every housekeeping pass.
static void draw_status_line(void) {
    static bool    init = false;
    static bool    last_conn = false;
    static bool    last_24g = false;
    static bool    last_usb = false;
    static uint8_t last_slot = 0xFF;
    static uint8_t last_batt = 0xFE;

    bool    conn = ch582_is_connected();
    bool    g24  = ch582_is_24g();
    bool    usb  = ch582_is_usb();
    uint8_t slot = ch582_get_slot();
    uint8_t batt = ch582_get_battery();
    if (init && conn == last_conn && g24 == last_24g && usb == last_usb &&
        slot == last_slot && batt == last_batt) return;
    init      = true;
    last_conn = conn;
    last_24g  = g24;
    last_usb  = usb;
    last_slot = slot;
    last_batt = batt;

    // Clear the bottom strip (match the green dashboard background).
    qp_rect(qp_display, 0, STATUS_Y, PANEL_WIDTH - 1, PANEL_HEIGHT - 1, 0, 255, 0, true);

    char buf[21];
    if (usb) {
        snprintf(buf, sizeof(buf), "USB");
    } else if (g24) {
        snprintf(buf, sizeof(buf), conn ? "2.4G" : "2.4G?");
    } else if (conn && slot) {
        snprintf(buf, sizeof(buf), "BT%u", slot);
    } else if (conn) {
        snprintf(buf, sizeof(buf), "BT");
    } else {
        snprintf(buf, sizeof(buf), "idle");
    }
    qp_drawtext(qp_display, 0, STATUS_Y, qp_status_font, buf);

    if (batt <= 100) {
        char bbuf[8];
        snprintf(bbuf, sizeof(bbuf), "%u%%", batt);
        int16_t w = qp_textwidth(qp_status_font, bbuf);
        qp_drawtext(qp_display, PANEL_WIDTH - 1 - w, STATUS_Y, qp_status_font, bbuf);
    }
}

void display_housekeeping_task(void) {
    // Call the user-defined housekeeping task first. If it returns false, skip the default housekeeping.
    if(!display_housekeeping_task_user())
        return;

    if(splash_cleared) {
        draw_clock();
        draw_status_line();
    }

    qp_flush(qp_display);
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
