#include "display.h"

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

// Keep track of the last second drawn to prevent screen spam
static uint8_t last_drawn_second = 60; // Start at 60 to force an immediate draw on boot

void draw_clock(void) {
    uint8_t hours, minutes, seconds;

    uint32_t current_ms = timer_read32();
    uint32_t total_seconds = current_ms / 1000;

    hours   = (total_seconds / 3600) % 24;
    minutes = (total_seconds / 60) % 60;
    seconds = total_seconds % 60;

    // Only update the screen if the second has actually changed
    if (seconds != last_drawn_second) {
        last_drawn_second = seconds;

        // Format the string to 00:00:00
        char time_str[9]; // 8 characters + null terminator
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hours, minutes, seconds);

        // Draw the new time text
        // (display, x, y, font, text)
        qp_drawtext(qp_display, (PANEL_WIDTH - qp_textwidth(qp_font, time_str))/2, 49, qp_font, time_str);
    }
}

uint32_t display_redraw_dashboard(uint32_t trigger_time, void *cb_arg) {

    splash_cleared = true;

    // Clear background
    qp_rect(qp_display, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, 0, 255, 0, true);

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
