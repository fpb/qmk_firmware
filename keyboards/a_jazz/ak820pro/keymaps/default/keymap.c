/* Copyright (C) 2023 Fernando Birra
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H

#include "display.h"

#include "ch582f_ajazz.h"
#include "connection.h"

enum layer_names {
    WINBASE,
    WINFN,
    MACBASE,
    MACFN
};

enum custom_keycodes {
    WIN_LOCK = SAFE_RANGE,
    SCR_TOG,
    BT1,       // Fn+Q: select BT slot 1
    BT2,       // Fn+W: select BT slot 2
    BT3,       // Fn+E: select BT slot 3
    BT24G,     // Fn+R: select 2.4G
    BT_PAIR    // Fn+P: enter pairing on the currently-selected slot
};

#define KC_TASK LGUI(KC_TAB)        // Task viewer
#define KC_FLXP LGUI(KC_E)          // Windows file explorer
#define KC_MCTL KC_MISSION_CONTROL  // Mission Control
#define KC_LPAD KC_LAUNCHPAD        // Launchpad

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    /* Windows Base
     * ┌───┬┬───┬───┬───┬───┬┬───┬───┬───┬───┬┬───┬───┬───┬───┬┬───┬┬───┐
     * │Esc││F1 │F2 │F3 │F4 ││F5 │F6 │F7 │F8 ││F9 │F10│F11│F12││DEL││VOL│
     * ├───┼┴──┬┴──┬┴──┬┴──┬┴┴─┬─┴─┬─┴─┬─┴─┬─┴┴┬──┴┬──┴┬──┴┬──┴┴──┬┴┼───┤
     * │ ~ │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 0 │ - │ = │Backsp│ │HOM│
     * ├───┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬────┼─┼───┤
     * │ Tab │ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │ [ │ ] │  \ │ │PGU│
     * ├─────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴────┼─┼───┤
     * │ Caps │ A │ S │ D │ F │ G │ H │ J │ K │ L │ ; │ ' │ Enter │ │PGD│
     * ├──────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴────┬┬─┴─┼───┤
     * │ Shift  │ Z │ X │ C │ V │ B │ N │ M │ , │ . │ / │ Shift││ ↑ │   │
     * ├────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴──┬┴──┬┴──┬┬──┴┼───┼───┤
     * │Ctrl│GUI │Alt │         Space          │Alt│Fn │Ctl││ ← │ ↓ │ → │
     * └────┴────┴────┴────────────────────────┴───┴───┴───┴┴───┴───┴───┘
     */
    [WINBASE] = LAYOUT_82_ansi(
        KC_ESC,     KC_F1,      KC_F2,      KC_F3,      KC_F4,      KC_F5,      KC_F6,      KC_F7,      KC_F8,      KC_F9,      KC_F10,     KC_F11,      KC_F12,     KC_DEL,     KC_MUTE,
        KC_GRV,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINS,     KC_EQL,     KC_BSPC,    KC_HOME,
        KC_TAB,     KC_Q,       KC_W,       KC_E,       KC_R,       KC_T,       KC_Y,       KC_U,       KC_I,       KC_O,       KC_P,       KC_LBRC,     KC_RBRC,    KC_BSLS,    KC_PGUP,
        KC_CAPS,    KC_A,       KC_S,       KC_D,       KC_F,       KC_G,       KC_H,       KC_J,       KC_K,       KC_L,       KC_SCLN,    KC_QUOT,                 KC_ENT,     KC_PGDN,
        KC_LSFT,                KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       KC_N,       KC_M,       KC_COMM,    KC_DOT,     KC_SLSH,     KC_RSFT,    KC_UP,
        KC_LCTL,    KC_LGUI,    KC_LALT,                                        KC_SPC,                             KC_RALT,    MO(WINFN),  KC_RCTL,     KC_LEFT,    KC_DOWN,    KC_RGHT
    ),
    /* Windows FN
     * ┌───┬┬───┬───┬───┬───┬┬───┬───┬───┬───┬┬───┬───┬───┬───┬┬───┬┬───┐
     * │Esc││BRU│BRD│TSK│FLX││VAD│VAI│PRV│PLY││NXT│MTE│VLD│VLU││   ││   │
     * ├───┼┴──┬┴──┬┴──┬┴──┬┴┴─┬─┴─┬─┴─┬─┴─┬─┴┴┬──┴┬──┴┬──┴┬──┴┴──┬┴┼───┤
     * │   │   │   │   │   │   │   │   │   │   │   │   │   │      │ │   │
     * ├───┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬────┼─┼───┤
     * │     │   │   │   │   │   │   │   │   │   │   │   │   │    │ │   │
     * ├─────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴────┼─┼───┤
     * │      │   │   │   │   │   │   │   │   │   │   │   │       │ │   │
     * ├──────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴────┬┬─┴─┼───┤
     * │        │   │   │   │   │   │   │   │   │   │   │      ││   │   │
     * ├────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴──┬┴──┬┴──┬┬──┴┼───┼───┤
     * │    │    │    │                        │   │   │   ││   │   │   │
     * └────┴────┴────┴────────────────────────┴───┴───┴───┴┴───┴───┴───┘
     */
    [WINFN] = LAYOUT_82_ansi(
        QK_BOOT,    KC_BRID,    KC_BRIU,    KC_TASK,    KC_FLXP,    _______,    _______,    KC_MPRV,    KC_MPLY,    KC_MNXT,    KC_MUTE,    KC_VOLD,     KC_VOLU,    SCR_TOG, KC_MUTE,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,     _______,    _______,    _______,
        _______,    BT1,        BT2,        BT3,        BT24G,      _______,    _______,    _______,    _______,    _______,    BT_PAIR,    _______,     _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,                 _______,    _______,
        _______,                _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,     _______,    _______,
        _______,    GU_TOGG,    _______,                                        _______,                            _______,    _______,    _______,     _______,    _______,    _______
    ),
    /* Mac Base
     * ┌───┬┬───┬───┬───┬───┬┬───┬───┬───┬───┬┬───┬───┬───┬───┬┬───┬┬───┐
     * │Esc││F1 │F2 │F3 │F4 ││F5 │F6 │F7 │F8 ││F9 │F10│F11│F12││DEL││VOL│
     * ├───┼┴──┬┴──┬┴──┬┴──┬┴┴─┬─┴─┬─┴─┬─┴─┬─┴┴┬──┴┬──┴┬──┴┬──┴┴──┬┴┼───┤
     * │ ~ │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 0 │ - │ = │Backsp│ │HOM│
     * ├───┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬────┼─┼───┤
     * │ Tab │ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │ [ │ ] │  \ │ │PGU│e
     * ├─────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴────┼─┼───┤
     * │ Caps │ A │ S │ D │ F │ G │ H │ J │ K │ L │ ; │ ' │ Enter │ │PGD│
     * ├──────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴────┬┬─┴─┼───┤
     * │ Shift  │ Z │ X │ C │ V │ B │ N │ M │ , │ . │ / │ Shift││ ↑ │   │
     * ├────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴──┬┴──┬┴──┬┬──┴┼───┼───┤
     * │Ctrl│Alt │GUI │         Space          │GUI│Fn │Ctl││ ← │ ↓ │ → │
     * └────┴────┴────┴────────────────────────┴───┴───┴───┴┴───┴───┴───┘
     */
    [MACBASE] = LAYOUT_82_ansi(
        KC_ESC,     KC_BRID,    KC_BRIU,    KC_MCTL,    KC_F4,      KC_F5,      KC_F6,      KC_MPRV,    KC_MPLY,    KC_MNXT,    KC_MUTE,    KC_VOLD,     KC_VOLU,    KC_DEL,     KC_MUTE,
        KC_GRV,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINS,     KC_EQL,     KC_BSPC,    KC_HOME,
        KC_TAB,     KC_Q,       KC_W,       KC_E,       KC_R,       KC_T,       KC_Y,       KC_U,       KC_I,       KC_O,       KC_P,       KC_LBRC,     KC_RBRC,    KC_BSLS,    KC_PGUP,
        KC_CAPS,    KC_A,       KC_S,       KC_D,       KC_F,       KC_G,       KC_H,       KC_J,       KC_K,       KC_L,       KC_SCLN,    KC_QUOT,                 KC_ENT,     KC_PGDN,
        KC_LSFT,                KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       KC_N,       KC_M,       KC_COMM,    KC_DOT,     KC_SLSH,     KC_RSFT,    KC_UP,
        KC_LCTL,    KC_LALT,    KC_LGUI,                                        KC_SPC,                             KC_RGUI,    MO(MACFN),  KC_RCTL,     KC_LEFT,    KC_DOWN,    KC_RGHT
    ),
    /* Mac FN
     * ┌───┬┬───┬───┬───┬───┬┬───┬───┬───┬───┬┬───┬───┬───┬───┬┬───┬┬───┐
     * │Esc││BRU│BRD│TSK│FLX││VAD│VAI│PRV│PLY││NXT│MTE│VLD│VLU││   ││   │
     * ├───┼┴──┬┴──┬┴──┬┴──┬┴┴─┬─┴─┬─┴─┬─┴─┬─┴┴┬──┴┬──┴┬──┴┬──┴┴──┬┴┼───┤
     * │   │   │   │   │   │   │   │   │   │   │   │   │   │      │ │   │
     * ├───┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬────┼─┼───┤
     * │     │   │   │   │   │   │   │   │   │   │   │   │   │    │ │   │
     * ├─────┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴────┼─┼───┤
     * │      │   │   │   │   │   │   │   │   │   │   │   │       │ │   │
     * ├──────┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴─┬─┴────┬┬─┴─┼───┤
     * │        │   │   │   │   │   │   │   │   │   │   │      ││   │   │
     * ├────┬───┴┬──┴─┬─┴───┴───┴───┴───┴───┴──┬┴──┬┴──┬┴──┬┬──┴┼───┼───┤
     * │    │    │    │                        │   │   │   ││   │   │   │
     * └────┴────┴────┴────────────────────────┴───┴───┴───┴┴───┴───┴───┘
     */
    [MACFN] = LAYOUT_82_ansi(
        QK_BOOT,    KC_F1,      KC_F2,      KC_F3,      KC_F4,      KC_F5,      KC_F6,      KC_F7,      KC_F8,      KC_F9,      KC_F10,     KC_F11,     KC_F12,      SCR_TOG,    KC_MUTE,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,     _______,    _______,    _______,
        _______,    BT1,        BT2,        BT3,        BT24G,      _______,    _______,    _______,    _______,    _______,    BT_PAIR,    _______,     _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,                 _______,    _______,
        _______,                _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,     _______,    _______,
        _______,    _______,    _______,                                        _______,                            _______,    _______,    _______,     _______,    _______,    _______
    )
};


//bool win_lock_active = false;

/* BT controls:
 *   - Fn+Q/W/E select BT slots 1/2/3 (A6 3x).
 *   - Fn+R selects 2.4G.
 *   - Fn+P enters pairing on the currently-selected slot (A6 51). */

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

bool dip_switch_update_user(uint8_t index, bool active) {
    if (index == 0) {
        if (active) {
            set_single_persistent_default_layer(WINBASE);
        } else {
            set_single_persistent_default_layer(MACBASE);
            keymap_config.no_gui = false;
        }
    }
    // The mode slider is a tri-state encoded by two dip pins: index 1 = BT,
    // index 2 = 2.4G, both inactive = USB. We must look at BOTH pins together --
    // handling them independently lets the inactive sibling's "else" branch call
    // ch582_cancel_connect() and clobber connect_requested even while the other
    // mode is active (e.g. at boot in BT position), which silently disables
    // wireless key forwarding. Recompute the mode from the latched pin states.
    if (index == 1 || index == 2) {
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
            // Bluetooth mode: default to slot 1.
            wireless_mode = WL_MODE_BT;
            ch582_set_profile(CH582_PROFILE_BT_1);
            connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
        } else if (g24_on) {
            // 2.4GHz mode: select the 2.4G channel ('0').
            wireless_mode = WL_MODE_24G;
            ch582_set_profile(CH582_PROFILE_PEER_24G);
            connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
        } else {
            // USB mode (both inactive): stop retrying, but keep the module alive
            // (it must stay powered to keep reporting battery level). Route key
            // reports back to USB.
            wireless_mode = WL_MODE_USB;
            ch582_cancel_connect();
            connection_set_host_noeeprom(CONNECTION_HOST_USB);
        }
    }
    return true;
}


bool process_record_user(uint16_t keycode, keyrecord_t *record) {

    switch (keycode) {
        case KC_MISSION_CONTROL:
            if (record->event.pressed) {
                host_consumer_send(0x29F);
            } else {
                host_consumer_send(0);
            }
            return false;  // Skip all further processing of this key
        case KC_LAUNCHPAD:
            if (record->event.pressed) {
                host_consumer_send(0x2A0);
            } else {
                host_consumer_send(0);
            }
            return false;  // Skip all further processing of this key
        case SCR_TOG:
            if (record->event.pressed) {
                display_toggle_power();
            }
            return false;  // Skip all further processing of this key
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
            return true;  // Process all other keycodes normally
    }
}


#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [WINBASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU) },
    [WINFN] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU) },
    [MACBASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU) },
    [MACFN] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU) }
};
#endif

/* bluetooth_init() / bluetooth_task() are now invoked by QMK core
 * (quantum/keyboard.c) under BLUETOOTH_ENABLE, so no manual ch582 init/task
 * pumping here. */
