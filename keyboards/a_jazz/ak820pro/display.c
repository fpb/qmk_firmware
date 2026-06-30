#include "display.h"

#include "rtc.h"

#include "graphics/sonixqmk.qgf.h"
#include "graphics/Iosevka-Regular-30.qff.h"
#include "graphics/robotomono20.qff.h"

#include "graphics/apple_icon_24x24.qgf.h"
#include "graphics/windows_icon_24x24.qgf.h"
#include "graphics/cable_icon_24x24.qgf.h"
#include "graphics/bluetooth_icon_24x24.qgf.h"
#include "graphics/2_4_g_icon_24x24.qgf.h"

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

// y position of the big HH:MM:SS clock (top of the glyphs).
#define CLOCK_Y 49

// Force-redraw markers; reset on a full dashboard repaint and when leaving edit.
static uint8_t last_drawn_second = 60; // 60 forces an immediate draw
static bool    date_drawn        = false;

// The software clock: a full date+time captured at base_tick, advanced purely
// by the MCU timer (no periodic RTC resync -- bit-banged I2C would add keystroke
// latency). base rolls forward across midnight so the date stays correct.
static bool       have_base = false;
static rtc_time_t base;            // date+time as of base_tick
static uint32_t   base_tick = 0;   // timer_read32() captured at the seed/commit
static uint32_t   last_try  = 0;   // throttles the boot seed retries

// --- Date arithmetic (for midnight rollover and field editing). -------------
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

// --- Clock edit mode: Fn+Knob steps through fields, knob rotation adjusts the
//     selected one; the RTC is written only when the last field is committed. --
typedef enum { EF_DAY, EF_MONTH, EF_HOUR, EF_MIN, EF_SEC, EF_COUNT } edit_field_t;
static bool         edit_active = false;
static edit_field_t edit_field;
static rtc_time_t   edit_time; // working copy, not written until commit

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

bool clock_edit_active(void) { return edit_active; }

// Fn+Knob press: enter edit mode (on the day field), advance to the next field,
// or -- after the last field -- commit the working copy to base + RTC and exit.
void clock_edit_step(void) {
    if (!edit_active) {
        clock_current(&edit_time);
        if (edit_time.day == 0)   edit_time.day = 1;     // sane defaults if the
        if (edit_time.month == 0) edit_time.month = 1;   // RTC never seeded
        if (edit_time.year == 0)  edit_time.year = 2025;
        edit_active = true;
        edit_field  = EF_DAY;
    } else if (++edit_field == EF_COUNT) {
        edit_active = false;
        base        = edit_time;
        base_tick   = timer_read32();
        have_base   = true;
        rtc_set_time(&edit_time); // single write, clears VL
        last_drawn_second = 60;   // force a clean repaint of the live clock
        date_drawn        = false;
    }
}

// Knob rotation while editing: +/- the selected field, with wrap and day clamp.
void clock_edit_adjust(int8_t dir) {
    if (!edit_active) return;
    switch (edit_field) {
        case EF_DAY: {
            uint8_t dim = days_in_month(edit_time.month, edit_time.year);
            edit_time.day = (uint8_t)((edit_time.day - 1 + dir + dim) % dim) + 1;
            break;
        }
        case EF_MONTH: {
            edit_time.month = (uint8_t)((edit_time.month - 1 + dir + 12) % 12) + 1;
            uint8_t dim = days_in_month(edit_time.month, edit_time.year);
            if (edit_time.day > dim) edit_time.day = dim;
            break;
        }
        case EF_HOUR:
            edit_time.hours = (uint8_t)((edit_time.hours + dir + 24) % 24);
            break;
        case EF_MIN:
            edit_time.minutes = (uint8_t)((edit_time.minutes + dir + 60) % 60);
            break;
        case EF_SEC:
            edit_time.seconds = (uint8_t)((edit_time.seconds + dir + 60) % 60);
            break;
        default:
            break;
    }
}

// Draw one field, blanking it (to the green background) when it is the active
// edit field on a blink-off phase -- gives a flicker-free selection cue.
static void draw_field(uint16_t x, uint16_t y, painter_font_handle_t font,
                       const char *txt, bool hide) {
    if (hide) {
        qp_rect(qp_display, x, y, x + qp_textwidth(font, txt), y + font->line_height,
                0, 255, 0, true);
    } else {
        qp_drawtext(qp_display, x, y, font, txt);
    }
}

// Render the editable date (top-right) and time (centre) field-by-field, so the
// active field can blink without disturbing the others.
static void draw_edit(void) {
    bool off = (timer_read32() / 400) & 1; // ~1.25 Hz blink of the active field

    char d2[4], mo2[4], hh[4], mm[4], ss[4];
    snprintf(d2,  sizeof(d2),  "%02u", (unsigned)edit_time.day);
    snprintf(mo2, sizeof(mo2), "%02u", (unsigned)edit_time.month);
    snprintf(hh,  sizeof(hh),  "%02u", (unsigned)edit_time.hours);
    snprintf(mm,  sizeof(mm),  "%02u", (unsigned)edit_time.minutes);
    snprintf(ss,  sizeof(ss),  "%02u", (unsigned)edit_time.seconds);

    // Date DD/MM, small font, top-right.
    int16_t dw  = qp_textwidth(qp_status_font, "00");
    int16_t slw = qp_textwidth(qp_status_font, "/");
    int16_t dx  = PANEL_WIDTH - 1 - (2 * dw + slw);
    draw_field(dx, 2, qp_status_font, d2, edit_field == EF_DAY && off);
    qp_drawtext(qp_display, dx + dw, 2, qp_status_font, "/");
    draw_field(dx + dw + slw, 2, qp_status_font, mo2, edit_field == EF_MONTH && off);

    // Time HH:MM:SS, big font, centred.
    int16_t cw = qp_textwidth(qp_font, "00");
    int16_t co = qp_textwidth(qp_font, ":");
    int16_t tx = (PANEL_WIDTH - qp_textwidth(qp_font, "00:00:00")) / 2;
    draw_field(tx, CLOCK_Y, qp_font, hh, edit_field == EF_HOUR && off);
    qp_drawtext(qp_display, tx + cw, CLOCK_Y, qp_font, ":");
    draw_field(tx + cw + co, CLOCK_Y, qp_font, mm, edit_field == EF_MIN && off);
    qp_drawtext(qp_display, tx + 2 * cw + co, CLOCK_Y, qp_font, ":");
    draw_field(tx + 2 * cw + 2 * co, CLOCK_Y, qp_font, ss, edit_field == EF_SEC && off);
}

void draw_clock(void) {
    uint32_t now = timer_read32();

    // Seed the clock from the external PCF8563 RTC ONCE at boot, then free-run on
    // the MCU timer. After one good read we never touch the bus again; until then
    // retry at most once a second (not every housekeeping pass), showing uptime.
    if (!have_base && !edit_active &&
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

    if (edit_active) { draw_edit(); return; }

    rtc_time_t shown;
    clock_current(&shown);

    // Time HH:MM:SS, centred -- only when the second changes, to spare the SPI.
    if (shown.seconds != last_drawn_second) {
        last_drawn_second = shown.seconds;
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u",
                 (unsigned)shown.hours, (unsigned)shown.minutes, (unsigned)shown.seconds);
        qp_drawtext(qp_display, (PANEL_WIDTH - qp_textwidth(qp_font, time_str)) / 2,
                    CLOCK_Y, qp_font, time_str);
    }

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
        4, //spi_divisor,
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
