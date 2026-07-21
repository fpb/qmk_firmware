// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ak820pro.h"

#include "gpio.h"
#include "connection.h"

#include "graphics/display.h"
#include "bluetooth/ch582f_ajazz.h"
#include "rtc/rtc.h"
#include "raw_hid.h"

// Current wireless mode, derived from the tri-state slider. The Fn BT controls
// are only meaningful in the matching mode (e.g. Fn+Q selects a BT slot only
// while the slider is in the BT position), so they are gated on this.
enum wireless_mode {
    WL_MODE_USB = 0,
    WL_MODE_BT,
    WL_MODE_24G
};
static uint8_t wireless_mode = WL_MODE_USB;

// Persisted keyboard config (EEPROM kb datablock; 4 bytes reserved, room to grow).
typedef struct __attribute__((packed)) {
    uint8_t bt_profile;   // last BT slot selected (CH582_PROFILE_BT_1..3)
    uint8_t _pad[3];
} kb_config_t;
static kb_config_t kb_config;

// Last BT slot the user selected. Entering BT mode used to hardcode slot 1, so
// leaving BT for USB/2.4G and coming back silently dropped you onto slot 1
// whatever you had been connected to. Persisted to EEPROM so it survives mode
// switches and power cycles.
static ch582_profile_t last_bt_profile = CH582_PROFILE_BT_1;

// Write the remembered slot back only when it actually changed -- wear-leveling
// on internal flash is cheap but not free, and slot changes are user-initiated.
static void save_bt_profile(ch582_profile_t p) {
    if (p == last_bt_profile) return;
    last_bt_profile     = p;
    kb_config.bt_profile = (uint8_t)p;
    eeconfig_update_kb_datablock(&kb_config, 0, sizeof(kb_config));
}

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
    // Windows Lock and Charging LEDs: outputs, off initially. update_leds() then
    // tracks their real state, writing only on a change.
    gpio_set_pin_output(LED_WINLOCK_PIN);
    gpio_write_pin(LED_WINLOCK_PIN, false);
    gpio_set_pin_output(LED_CHARGING_PIN);
    gpio_write_pin(LED_CHARGING_PIN, false);

    // Set up GPIO pins for the charging status inputs
    gpio_set_pin_input_high(CHARGE_CHRG_PIN);   // input with pull-up
    gpio_set_pin_input_high(CHARGE_STDBY_PIN);  // input with pull-up

    // Restore persisted config. A fresh/invalid EEPROM zero-fills the block, so a
    // 0 (or out-of-range) bt_profile falls back to slot 1.
    eeconfig_read_kb_datablock(&kb_config, 0, sizeof(kb_config));
    if (kb_config.bt_profile >= CH582_PROFILE_BT_1 && kb_config.bt_profile <= CH582_PROFILE_BT_3)
        last_bt_profile = (ch582_profile_t)kb_config.bt_profile;

    // dip_switch_init() runs BEFORE this hook, so its boot-time BT selection used
    // the default slot (EEPROM had not been read yet). Now that we have the saved
    // slot, re-select it if we booted with the slider already on Bluetooth.
    if (wireless_mode == WL_MODE_BT)
        ch582_set_profile(last_bt_profile);

    // Bring up the clock: I2C, the SN32 1 Hz counter, and a first PCF8563 seed.
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
            ch582_set_profile(last_bt_profile);  // resume the slot last selected
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
#ifdef RGB_MATRIX_ENABLE
        // VIA-assignable RGB-matrix controls (see ak820pro.h). One step per press.
        case RGBM_TOG:  if (record->event.pressed) rgb_matrix_toggle();         return false;
        case RGBM_MOD:  if (record->event.pressed) rgb_matrix_step();           return false;
        case RGBM_RMOD: if (record->event.pressed) rgb_matrix_step_reverse();   return false;
        case RGBM_HUI:  if (record->event.pressed) rgb_matrix_increase_hue();   return false;
        case RGBM_HUD:  if (record->event.pressed) rgb_matrix_decrease_hue();   return false;
        case RGBM_SAI:  if (record->event.pressed) rgb_matrix_increase_sat();   return false;
        case RGBM_SAD:  if (record->event.pressed) rgb_matrix_decrease_sat();   return false;
        case RGBM_VAI:  if (record->event.pressed) rgb_matrix_increase_val();   return false;
        case RGBM_VAD:  if (record->event.pressed) rgb_matrix_decrease_val();   return false;
        case RGBM_SPI:  if (record->event.pressed) rgb_matrix_increase_speed(); return false;
        case RGBM_SPD:  if (record->event.pressed) rgb_matrix_decrease_speed(); return false;
#endif
        case BT1:  // Fn+Q -> select BT slot 1 (BT mode only)
            if (record->event.pressed && wireless_mode == WL_MODE_BT) {
                save_bt_profile(CH582_PROFILE_BT_1);
                ch582_set_profile(last_bt_profile);
                connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
                display_draw_bluetooth_logo();
            }
            return false;
        case BT2:  // Fn+W -> select BT slot 2 (BT mode only)
            if (record->event.pressed && wireless_mode == WL_MODE_BT) {
                save_bt_profile(CH582_PROFILE_BT_2);
                ch582_set_profile(last_bt_profile);
                connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
                display_draw_bluetooth_logo();
            }
            return false;
        case BT3:  // Fn+E -> select BT slot 3 (BT mode only)
            if (record->event.pressed && wireless_mode == WL_MODE_BT) {
                save_bt_profile(CH582_PROFILE_BT_3);
                ch582_set_profile(last_bt_profile);
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

// Apply a 7-byte time payload to the RTC:
//   [0]=year-2000 [1]=month [2]=day [3]=weekday [4]=hour [5]=min [6]=sec
// Sets both the PCF8563 (persist) and the live SN32 clock; the display picks it up
// within a second via rtc_get_time(). Returns the PCF (persistence) write status.
static bool rtc_apply_bytes(const uint8_t *p) {
    rtc_time_t t = {
        .year    = (uint16_t)(2000 + p[0]),
        .month   = p[1],
        .day     = p[2],
        .weekday = p[3],
        .hours   = p[4],
        .minutes = p[5],
        .seconds = p[6],
    };
    return rtc_set_time(&t);
}

// Clock-set command framing, identical for VIA and non-VIA builds so the host
// set-clock utility speaks ONE protocol. It's VIA's custom-value layout:
//   [SET_VALUE, RTC_CHANNEL, RTC_SET_TIME, year-2000, month, day, weekday,
//    hour, min, sec]
// The reply echoes the packet: data[0] stays SET_VALUE when handled, or becomes
// UNHANDLED (0xFF) when rejected. SET_VALUE/UNHANDLED mirror VIA's
// id_custom_set_value / id_unhandled so the same bytes work against either build.
enum {
    RTC_SET_VALUE = 0x07, // == VIA id_custom_set_value
    RTC_UNHANDLED = 0xFF, // == VIA id_unhandled
    RTC_CHANNEL   = 0x10,
    RTC_SET_TIME  = 0x01,
};

static inline bool rtc_is_set_time_cmd(const uint8_t *data, uint8_t length) {
    return length >= 10 && data[0] == RTC_SET_VALUE &&
           data[1] == RTC_CHANNEL && data[2] == RTC_SET_TIME;
}

#if defined(VIA_ENABLE)

// VIA owns raw_hid_receive() and dispatches custom-value commands here. VIA echoes
// the buffer back itself -- do NOT call raw_hid_send().
void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    if (rtc_is_set_time_cmd(data, length)) {
        rtc_apply_bytes(&data[3]); // leave data[0] = SET_VALUE -> "handled"
        return;
    }
    data[0] = RTC_UNHANDLED;
}

#else // no VIA: handle the same packet directly and echo it back like VIA would.

void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (rtc_is_set_time_cmd(data, length)) {
        rtc_apply_bytes(&data[3]);
    } else {
        data[0] = RTC_UNHANDLED;
    }
    raw_hid_send(data, length);
}

#endif


static void update_leds(void) {
    // Charging LED: on only while actively charging -- CHRG low (active) AND
    // STDBY high (not "done").
    static bool last_chrg = false;
    bool is_charging = !gpio_read_pin(CHARGE_CHRG_PIN) && gpio_read_pin(CHARGE_STDBY_PIN);
    if (is_charging != last_chrg) {
        gpio_write_pin(LED_CHARGING_PIN, is_charging);
        last_chrg = is_charging;
    }

    // Windows Lock LED: mirrors the GUI-lock flag.
    static bool last_winlock = false;
    bool winlock = keymap_config.no_gui;
    if (winlock != last_winlock) {
        gpio_write_pin(LED_WINLOCK_PIN, winlock);
        last_winlock = winlock;
    }
}

__attribute__((weak)) void display_housekeeping_task(void) {}

void housekeeping_task_kb(void) {

    // Throttle the housekeeping to 10 Hz
    static uint32_t last_t = 0;
    if (timer_elapsed32(last_t) >= 100) {
        last_t = timer_read32();

        update_leds();
        rtc_task();
        display_housekeeping_task();
    }

    // Chain the user hook
    housekeeping_task_user();
}
