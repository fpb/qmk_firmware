// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ak820pro.h"

#include "gpio.h"
#include "connection.h"

#include "graphics/display.h"
#include "bluetooth/ch582f_ajazz.h"
#include "rtc/rtc.h"
#include "raw_hid.h"

#ifdef RGB_MATRIX_ENABLE
// Per-key LED map (81 LEDs; the encoder/knob at matrix [0][14] has no LED).
// Generated from the LAYOUT_82_ansi coordinates: matrix_co[row][col] -> LED index,
// physical positions scaled to QMK's 0-224 x / 0-64 y space.
led_config_t g_led_config = {
    {   // matrix_co [MATRIX_ROWS][MATRIX_COLS]
        {      0,      1,      2,      3,      4,      5,      6,      7,      8,      9,     10,     11,     12,     13, NO_LED },
        {     14,     15,     16,     17,     18,     19,     20,     21,     22,     23,     24,     25,     26,     27,     28 },
        {     29,     30,     31,     32,     33,     34,     35,     36,     37,     38,     39,     40,     41,     42,     43 },
        {     44,     45,     46,     47,     48,     49,     50,     51,     52,     53,     54,     55, NO_LED,     56,     57 },
        {     58, NO_LED,     59,     60,     61,     62,     63,     64,     65,     66,     67,     68,     69,     70, NO_LED },
        {     71,     72,     73, NO_LED, NO_LED, NO_LED,     74, NO_LED, NO_LED,     75,     76,     77,     78,     79,     80 },
    },
    {   // physical positions [81] -- key centers mapped to QMK 0-224 x / 0-64 y
        {  7, 5}, { 24, 5}, { 38, 5}, { 52, 5}, { 66, 5}, { 84, 5}, { 98, 5}, {112, 5},
        {126, 5}, {144, 5}, {158, 5}, {172, 5}, {186, 5}, {203, 5}, {  7,19}, { 21,19},
        { 35,19}, { 49,19}, { 63,19}, { 77,19}, { 91,19}, {105,19}, {119,19}, {133,19},
        {147,19}, {161,19}, {175,19}, {196,19}, {224,19}, { 10,29}, { 28,29}, { 42,29},
        { 56,29}, { 70,29}, { 84,29}, { 98,29}, {112,29}, {126,29}, {140,29}, {154,29},
        {168,29}, {182,29}, {200,29}, {224,29}, { 12,40}, { 32,40}, { 46,40}, { 60,40},
        { 74,40}, { 88,40}, {102,40}, {116,40}, {130,40}, {144,40}, {158,40}, {172,40},
        {194,40}, {224,40}, { 16,51}, { 38,51}, { 52,51}, { 66,51}, { 80,51}, { 94,51},
        {108,51}, {122,51}, {136,51}, {150,51}, {164,51}, {184,51}, {206,53}, {  9,61},
        { 26,61}, { 44,61}, { 96,61}, {147,61}, {161,61}, {175,61}, {189,64}, {203,64},
        {217,64},
    },
    {   // flags [81]
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, // LED_FLAG_KEYLIGHT (4) x81
    },
};
#endif

// Current wireless mode, derived from the tri-state slider. The Fn BT controls
// are only meaningful in the matching mode (e.g. Fn+Q selects a BT slot only
// while the slider is in the BT position), so they are gated on this.
enum wireless_mode {
    WL_MODE_USB = 0,
    WL_MODE_BT,
    WL_MODE_24G
};
static uint8_t wireless_mode = WL_MODE_USB;

// Fn+P long-press tracking: pairing only starts after a sustained hold, and
// only while in a wireless mode.
#define BT_PAIR_HOLD_MS 1000
static uint16_t bt_pair_timer = 0;
static bool     bt_pair_armed = false;

void early_hardware_init_post(void) {
    // Configure SPI0 pins for the LCD panel
    SN_PFPA->SPI_b.MISO0 = 0b11;
    SN_PFPA->SPI_b.MOSI0 = 0b11;
    SN_PFPA->SPI_b.SCK0  = 0b11;
    SN_PFPA->SPI_b.SEL0  = 0b10;

    // Configure UART2 pins for the CH582F wireless module
    SN_PFPA->UART_b.UTXD2 = 0b11;
    SN_PFPA->UART_b.URXD2 = 0b11;
}

 void keyboard_post_init_kb(void) {
    // Set up GPIO pins for the Windows Lock and Charging LEDs
    gpio_set_pin_output(LED_WINLOCK_PIN);
    gpio_set_pin_output(LED_CHARGING_PIN);

    // Set up GPIO pins for the charging status inputs
    gpio_set_pin_input_high(CHARGE_CHRG_PIN);   // input with pull-up
    gpio_set_pin_input_high(CHARGE_STDBY_PIN);  // input with pull-up

    // Bring the bit-banged I2C lines to idle before the first RTC read.
    rtc_init();

    // Initialize the display subsystem (painter, fonts, images, etc.) and draw the splash screen.
    display_init_kb();

    // Chain the user hook: overriding keyboard_post_init_kb() replaces QMK's
    // default, which is what normally calls keyboard_post_init_user().
    keyboard_post_init_user();
 }

 bool dip_switch_update_kb(uint8_t index, bool active) {
    // Let the keymap handle layer logic (Mac/Win, no_gui) first.
    if(!dip_switch_update_user(index, active)) {
        return false;
    }

    if (index == 0) {  // Mac/Windows switch -- icon only (layer set by keymap)
        if (active) display_draw_windows_logo();
        else        display_draw_mac_logo();
    } else if (index == 1 || index == 2) {
        // The mode slider is a tri-state encoded by two dip pins: index 1 = BT,
        // index 2 = 2.4G, both inactive = USB. We must look at BOTH pins together
        // -- handling them independently lets the inactive sibling's "else" branch
        // call ch582_cancel_connect() and clobber connect_requested even while the
        // other mode is active (e.g. at boot in BT position), silently disabling
        // wireless key forwarding. Recompute the mode from the latched pin states.
        static bool bt_on  = false;
        static bool g24_on = false;
        if (index == 1) bt_on = active;
        if (index == 2) g24_on = active;

        // The CH582F handles both BT and 2.4G over the same UART, and QMK's
        // CONNECTION_HOST_2P4GHZ is not wired, so BOTH wireless positions map to
        // CONNECTION_HOST_BLUETOOTH (QMK then routes key reports to bt_driver ->
        // our bluetooth_send_keyboard). Our own A6 profile-select tells the
        // module which radio to use.
        if (bt_on) {
            wireless_mode = WL_MODE_BT;
            ch582_set_profile(CH582_PROFILE_BT_1);  // BT mode: default to slot 1
            connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
            display_draw_bluetooth_logo();
        } else if (g24_on) {
            wireless_mode = WL_MODE_24G;
            ch582_set_profile(CH582_PROFILE_PEER_24G);
            connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
            display_draw_2_4_g_logo();
        } else {
            // USB mode (both inactive): stop retrying, but keep the module alive
            // (it must stay powered to keep reporting battery level). Route key
            // reports back to USB.
            wireless_mode = WL_MODE_USB;
            ch582_cancel_connect();
            connection_set_host_noeeprom(CONNECTION_HOST_USB);
            display_draw_usb_logo();
        }
    }
    return true;
 }

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    // Let the keymap handle/override keycodes first (e.g. Mac media keys).
    if (!process_record_user(keycode, record)) {
        return false;
    }

    switch (keycode) {
        case SCR_TOG:
            if (record->event.pressed) display_toggle_power();
            return false;
        case BT1:  // Fn+Q -> select BT slot 1 (BT mode only)
            if (record->event.pressed && wireless_mode == WL_MODE_BT) {
                ch582_set_profile(CH582_PROFILE_BT_1);
                connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
                display_draw_bluetooth_logo();
            }
            return false;
        case BT2:  // Fn+W -> select BT slot 2 (BT mode only)
            if (record->event.pressed && wireless_mode == WL_MODE_BT) {
                ch582_set_profile(CH582_PROFILE_BT_2);
                connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
                display_draw_bluetooth_logo();
            }
            return false;
        case BT3:  // Fn+E -> select BT slot 3 (BT mode only)
            if (record->event.pressed && wireless_mode == WL_MODE_BT) {
                ch582_set_profile(CH582_PROFILE_BT_3);
                connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
                display_draw_bluetooth_logo();
            }
            return false;
        case BT24G:  // Fn+R -> select 2.4G (2.4G mode only)
            if (record->event.pressed && wireless_mode == WL_MODE_24G) {
                ch582_set_profile(CH582_PROFILE_PEER_24G);
                connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
                display_draw_2_4_g_logo();
            }
            return false;
        case BT_PAIR:  // Fn+P (long press) -> pair, BT/2.4G modes only
            if (record->event.pressed) {
                // Arm a long-press only in a wireless mode; ignore in USB.
                bt_pair_armed = (wireless_mode != WL_MODE_USB);
                bt_pair_timer = timer_read();
            } else if (bt_pair_armed) {
                bt_pair_armed = false;
                if (timer_elapsed(bt_pair_timer) >= BT_PAIR_HOLD_MS) {
                    ch582_enter_pairing();
                }
            }
            return false;
        default:
            return true;
    }
}

// Raw HID: host -> keyboard command channel (used by set-clock utility to set the
// RTC). Report layout (32 bytes, no report ID on the wire):
//   [0]=command, then for 0x01 (set time):
//   [1]=year-2000 [2]=month [3]=day [4]=weekday [5]=hour [6]=min [7]=sec
// We reply in-place: [1] becomes a status byte (0x00 OK, 0xFF write failed,
// 0xFE unknown command) and echo the buffer back.
void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (length >= 8 && data[0] == 0x01) {
        rtc_time_t t = {
            .seconds = data[7],
            .minutes = data[6],
            .hours   = data[5],
            .day     = data[3],
            .weekday = data[4],
            .month   = data[2],
            .year    = (uint16_t)(2000 + data[1]),
        };
        if (rtc_set_time(&t)) {   // single write to the chip, clears VL
            display_clock_set(&t);        // write-through: live clock updates, no reboot
            data[1] = 0x00;
        } else {
            data[1] = 0xFF;
        }
    } else {
        data[1] = 0xFE;
    }
    raw_hid_send(data, length);
}

static inline void update_leds(void) {
    // Write each indicator only when its state changes (cheap reads each pass,
    // GPIO writes only on transitions) -- no time throttle needed.
    static int8_t last_chrg = -1, last_winlock = -1;

    // Charging LED logic: ON when charging, OFF otherwise
    bool chrg_active  = !gpio_read_pin(CHARGE_CHRG_PIN);
    bool stdby_active =  gpio_read_pin(CHARGE_STDBY_PIN);
    bool is_charging  = chrg_active && stdby_active;
    if (is_charging != last_chrg) {
        gpio_write_pin(LED_CHARGING_PIN, is_charging);
        last_chrg = is_charging;
    }

    // Windows Lock LED logic: ON when no_gui is true, OFF otherwise
    if (keymap_config.no_gui != last_winlock) {
        gpio_write_pin(LED_WINLOCK_PIN, keymap_config.no_gui);
        last_winlock = keymap_config.no_gui;
    }
}

__attribute__((weak)) void display_housekeeping_task(void) {}

void housekeeping_task_kb(void) {
    update_leds();
    display_control_power();

    display_housekeeping_task();

    // Chain the user hook: QMK's default housekeeping_task_kb() calls
    // housekeeping_task_user(), but overriding this function replaces that
    // default.
    housekeeping_task_user();
}
