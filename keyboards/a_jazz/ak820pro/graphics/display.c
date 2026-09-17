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
#include "media/media.h"



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

// Low-power display state, shared by host-suspend (A) and, later, the idle-sleep
// timer (B). Idempotent via s_slept so a spurious wake/enter is a no-op. Order on
// enter: quiesce the bus (stop the player, let any blit finish), stop the repaint,
// panel to sleep-in, backlight off. Exit reverses it and forces a full repaint.
static bool s_slept = false;
static bool s_anim_was_active = false;     // resume the animation on wake if it was playing

void display_enter_sleep(void) {
    if (s_slept) return;
    s_slept = true;
    s_anim_was_active = anim_active();
    if (s_anim_was_active) anim_toggle();  // stop the player (restores SPI0 + repaint)
    display_set_paused(true);              // stop the 10 Hz dashboard repaint
    while (lcd_blit_busy()) { /* let any in-flight blit drain */ }
    lcd_panel_sleep(true);                 // GC9107 display-off + sleep-in
    display_set_power(false);              // backlight off (the big visible draw)
}

void display_exit_sleep(void) {
    if (!s_slept) return;
    s_slept = false;
    lcd_panel_sleep(false);                // sleep-out (120 ms) + display-on
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

// Clock format: 1 = HH:MM:SS (per-second redraw of the changed cells), 0 = HH:MM.
#ifndef DISPLAY_CLOCK_SHOW_SECONDS
#    define DISPLAY_CLOCK_SHOW_SECONDS TRUE
#endif

// Forces a full clock+date repaint after the background is cleared.
static bool clock_force_repaint = true;

static void draw_status(bool force);       // connection digit + battery gauge
static bool nowplaying_view(void);         // media active and not force-cleared
static void draw_nowplaying(bool force);   // now-playing block (below the gauge)

// Date DD/MM, top-right of the status line -- shown in BOTH the clock and the
// now-playing view. Repaints on a forced full paint or when the date changes.
static void draw_date(bool force) {
    rtc_time_t shown;
    if (!rtc_get_time(&shown)) return;
    static uint8_t drawn_day = 0xFF, drawn_month = 0xFF;
    if (!force && shown.day == drawn_day && shown.month == drawn_month) return;
    drawn_day = shown.day; drawn_month = shown.month;
    char date_str[8];
    snprintf(date_str, sizeof(date_str), "%02u/%02u", (unsigned)shown.day, (unsigned)shown.month);
    uint16_t w = lcd_flash_text_width(FONT_STATUS, date_str);   // fixed DD/MM width -> overwrites cleanly
    lcd_draw_flash_text(FONT_STATUS, PANEL_WIDTH - 1 - w, 2, date_str);
}

// Big HH:MM(:SS) clock, drawn only in the clock view. Per-cell diff so a normal
// tick repaints just the seconds.
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
        uint8_t  n       = (uint8_t)strlen(time_str);
        uint16_t total_w = lcd_flash_text_width(FONT_CLOCK, time_str);
        int16_t  x0      = (PANEL_WIDTH - total_w) / 2;
        int16_t  cw      = total_w / n;
        for (uint8_t i = 0; i < n; i++) {
            if (time_str[i] != last_time[i]) {
                char ch[2] = {time_str[i], 0};
                lcd_draw_flash_text(FONT_CLOCK, x0 + i * cw, CLOCK_Y, ch);
            }
        }
        strcpy(last_time, time_str);
    }
    clock_force_repaint = false;
}

uint32_t display_redraw_dashboard(uint32_t trigger_time, void *cb_arg) {
    splash_cleared = true;

    // Clear background.
    lcd_clear_rect(0, 0, PANEL_WIDTH, PANEL_HEIGHT);

    // Full repaint: force the clock and date to redraw over the cleared screen.
    clock_force_repaint = true;

    // Mac/Windows icon (top-left).
    lcd_draw_flash_image(mac_mode ? ASSET_APPLE_ICON_24X24 : ASSET_WINDOWS_ICON_24X24, 0, 0);

    // Connection icon.
    if (connection_mode == CONN_MODE_WIRED)          lcd_draw_flash_image(ASSET_CABLE_ICON_24X24, 32, 0);
    else if (connection_mode == CONN_MODE_BLUETOOTH) lcd_draw_flash_image(ASSET_BLUETOOTH_ICON_24X24, 32, 0);
    else if (connection_mode == CONN_MODE_2_4G)      lcd_draw_flash_image(ASSET_2_4_G_ICON_24X24, 32, 0);

    draw_date(true);              // date on the status line (both views)
    draw_status(true);           // connection digit + battery gauge
    if (nowplaying_view()) draw_nowplaying(true);
    else                   draw_clock_time();

    return 0; // one-shot
}

bool display_init_kb(void) {
    lcd_init();          // GC9107 bring-up, rotation 270

    // Splash logo, held until the deferred dashboard repaint below.
    lcd_clear_rect(0, 0, PANEL_WIDTH, PANEL_HEIGHT);
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

#include "bluetooth/ch582f_ajazz.h"   // connection state enum + getters
extern uint8_t ch582_get_battery(void);

// General blink phase: true during the first half of each period_ms cycle, so any
// element can gate its visibility on it. period_ms == 0 means always on (solid).
bool display_blink(uint16_t period_ms) {
    if (!period_ms) return true;
    return (timer_read32() % period_ms) < (period_ms / 2u);
}

// Connection-digit blink rates.
#define CONN_BLINK_PAIRING_MS 200   // pairing: fast
#define CONN_BLINK_LINKING_MS 700   // linking/reconnecting: slow

// The top connection icon lives at (CONN_ICON_X, 0), 24x24. The channel digit is
// drawn just to its right.
#define CONN_ICON_X   32
#define CONN_ICON_W   24
#define CONN_NUM_X    (CONN_ICON_X + CONN_ICON_W + 1)
#define CONN_NUM_W    12

// Channel digit next to the connection icon, blinking to show link state:
//   connected -> solid digit;  linking/reconnecting -> slow blink;
//   pairing -> fast blink;  rejected / idle / USB -> no digit.
// Driven every housekeeping tick (~10 Hz) so the blink animates; the redraw is
// self-guarded so it only touches the panel on a visible transition.
static void draw_conn_number(bool force) {
    uint8_t slot  = ch582_get_target_slot();               // 1-3, or 0
    char    digit = (slot >= 1 && slot <= 3) ? (char)('0' + slot) : 0;

    char     c      = 0;   // the digit this state wants to show (0 = none)
    uint16_t period = 0;   // blink period; 0 = solid
    switch (ch582_get_conn_state()) {
        case CH582_CONN_CONNECTED: c = digit; period = 0;                     break;
        case CH582_CONN_PAIRING:   c = digit; period = CONN_BLINK_PAIRING_MS; break;
        case CH582_CONN_LINKING:   c = digit; period = CONN_BLINK_LINKING_MS; break;
        case CH582_CONN_REJECTED:  /* fall through */
        default:                   c = 0;                                     break;
    }
    char shown = (c && display_blink(period)) ? c : 0;     // current blink-phase visibility

    static char last_shown = -1; // force the first paint
    if (!force && shown == last_shown) return;
    last_shown = shown;

    lcd_clear_rect(CONN_NUM_X, 0, CONN_NUM_W + 1, CONN_ICON_W);
    if (shown) {
        char s[2] = {shown, 0};
        lcd_draw_flash_text(FONT_STATUS, CONN_NUM_X, 2, s);
    }
}

// --- battery gauge + now-playing zone ---------------------------------------
// The battery is now a full-width horizontal gauge just below the status line
// (replacing the old bottom "NN%" text), shown in BOTH the clock and now-playing
// views. Below it, the "zone" holds the clock OR the now-playing block.
#define GAUGE_Y   27
#define GAUGE_H   6
#define GX0       2
#define GX1       126
#define ZONE_Y0   36                 // top of the swappable content zone
#define NP_TITLE_Y1 40
#define NP_TITLE_Y2 64
#define NP_ARTIST_Y 90
#define NP_BAR_Y    118
#define NP_BAR_H    6
#define NP_CHARS    12               // Iosevka-Medium-20 chars per 128px line

#define COL_TRACK 0x2965             // dark grey (gauge/progress track)
#define COL_BATT  0x66EF             // green (battery fill)
#define COL_PROG  0x565F             // accent blue (progress fill)

// Full-width battery meter; repaints only on a level change (or force).
static void draw_battery_gauge(bool force) {
    static uint8_t last = 0xFE;
    uint8_t b = ch582_get_battery();
    if (b > 100) b = 0;
    if (!force && b == last) return;
    last = b;
    lcd_fill_rect(GX0, GAUGE_Y, GX1, GAUGE_Y + GAUGE_H, COL_TRACK);
    uint16_t fw = (uint16_t)((uint32_t)(GX1 - GX0) * b / 100u);
    if (fw) lcd_fill_rect(GX0, GAUGE_Y, GX0 + fw, GAUGE_Y + GAUGE_H, COL_BATT);
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
        } else if (cl == 0) {                 // word alone longer than a line
            memcpy(out[li], w, NP_CHARS); out[li][NP_CHARS] = 0;
            overflow = true; break;
        } else if (li == 0) {                 // wrap to line 2
            li = 1;
            uint8_t n = wl < NP_CHARS ? wl : NP_CHARS;
            memcpy(out[1], w, n); out[1][n] = 0;
        } else {
            overflow = true; break;           // would need a 3rd line
        }
    }
    while (*p == ' ') p++;
    if (overflow || *p) {                     // truncated -> ellipsis on the last line
        char *L = out[1][0] ? l2 : l1;
        uint8_t n = (uint8_t)strlen(L);
        if (n > NP_CHARS - 3) n = NP_CHARS - 3;
        L[n] = 0; strcat(L, "...");
    }
}

// One line of at most NP_CHARS, ellipsised if longer (for the artist).
static void fit_line(const char *s, char out[NP_CHARS + 1]) {
    uint8_t n = 0;
    while (s[n] && n < NP_CHARS) { out[n] = s[n]; n++; }
    out[n] = 0;
    if (s[n]) { if (n > NP_CHARS - 3) n = NP_CHARS - 3; out[n] = 0; strcat(out, "..."); }
}

// Progress bar only -- cheap, redrawn once/second as elapsed self-advances.
static void draw_np_progress(void) {
    uint32_t dur = media_duration_ms(), el = media_elapsed_ms();
    lcd_fill_rect(GX0, NP_BAR_Y, GX1, NP_BAR_Y + NP_BAR_H, COL_TRACK);
    uint16_t fw = dur ? (uint16_t)((uint32_t)(GX1 - GX0) * el / dur) : 0;
    if (fw > (GX1 - GX0)) fw = GX1 - GX0;
    if (fw) lcd_fill_rect(GX0, NP_BAR_Y, GX0 + fw, NP_BAR_Y + NP_BAR_H, COL_PROG);
}

// Full now-playing block: title (2 wrapped lines) + artist + progress. Repaints
// only on a real metadata change (or force) to avoid re-blitting text every tick.
static void draw_nowplaying(bool force) {
    if (!force && !media_take_dirty()) return;
    lcd_clear_rect(0, ZONE_Y0, PANEL_WIDTH, PANEL_HEIGHT - ZONE_Y0);

    char l1[NP_CHARS + 1], l2[NP_CHARS + 1];
    wrap_title(media_title(), l1, l2);
    if (l1[0]) lcd_draw_flash_text(FONT_STATUS, 2, NP_TITLE_Y1, l1);
    if (l2[0]) lcd_draw_flash_text(FONT_STATUS, 2, NP_TITLE_Y2, l2);

    char art[NP_CHARS + 1];
    fit_line(media_artist(), art);
    if (art[0]) lcd_draw_flash_text(FONT_STATUS, 2, NP_ARTIST_Y, art);   // white (flash tiles can't dim)

    draw_np_progress();
}

// View selection: now-playing when media is active and not force-cleared.
static bool s_force_clock = false;
static bool nowplaying_view(void) { return media_active() && !s_force_clock; }

// Keycode hook: toggle "force the clock" while media is playing.
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
    if (!display_housekeeping_task_user())
        return;

    if (display_paused) return;   // animation owns the bus
    if (!splash_cleared) return;

    // Connection digit every tick (~10 Hz) so its blink animates; self-guarded.
    draw_conn_number(false);

    // Switch views (media started/stopped, or the force-clock key) -> full repaint.
    static int8_t last_view = -1;
    int8_t view = nowplaying_view() ? 1 : 0;
    if (view != last_view) {
        last_view = view;
        display_redraw_dashboard(0, NULL);
        return;
    }

    // Metadata changes repaint the now-playing block immediately (self-guarded).
    if (view) draw_nowplaying(false);

    // Once per RTC second: advance the active view's time-driven bits + status.
    static uint32_t last_sec = UINT32_MAX;
    uint32_t sec = rtc_get_seconds();
    if (sec != last_sec) {
        last_sec = sec;
        draw_date(false);
        draw_battery_gauge(false);
        if (view) draw_np_progress();   // progress bar advances
        else      draw_clock_time();    // clock ticks
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
