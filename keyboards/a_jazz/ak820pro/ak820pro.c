#include "ak820pro.h"

#include "gpio.h"

#include "display.h"
#include "ch582f_ajazz.h"
#include "connection.h"

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

    // You can add any additional initialization code for your display here if needed
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

static inline void update_leds(void) {
    // Update leds every 100ms to avoid excessive GPIO calls
    static uint16_t last_check = 0;
    if (timer_elapsed(last_check) < 100) return;  // check every 100ms
    last_check = timer_read();

    // Charging LED logic: ON when charging, OFF otherwise
    bool chrg_active  = !gpio_read_pin(CHARGE_CHRG_PIN);
    bool stdby_active =  gpio_read_pin(CHARGE_STDBY_PIN);
    bool is_charging = chrg_active && stdby_active;
    gpio_write_pin(LED_CHARGING_PIN, is_charging);

    // Windows Lock LED logic: ON when no_gui is true, OFF otherwise
    gpio_write_pin(LED_WINLOCK_PIN, keymap_config.no_gui);
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
