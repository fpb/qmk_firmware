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

//#define CAPS_LOCK_LED   30

enum layer_names {
    WINBASE,
    WINFN,
    MACBASE,
    MACFN
};

enum custom_keycodes {
    WIN_LOCK = SAFE_RANGE
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
        _______,    KC_BRID,    KC_BRIU,    KC_TASK,    KC_FLXP,    _______,    _______,    KC_MPRV,    KC_MPLY,    KC_MNXT,    KC_MUTE,    KC_VOLD,     KC_VOLU,    KC_DEL,     KC_MUTE,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,     _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,     _______,    _______,    _______,
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
        _______,    KC_F1,      KC_F2,      KC_F3,      KC_F4,      KC_F5,      KC_F6,      KC_F7,      KC_F8,      KC_F9,      KC_F10,     KC_F11,     KC_F12,      KC_DEL,     KC_MUTE,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,     _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,     _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,                 _______,    _______,
        _______,                _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,     _______,    _______,    
        _______,    _______,    _______,                                        _______,                            _______,    _______,    _______,     _______,    _______,    _______
    )
};


//bool win_lock_active = false;

bool dip_switch_update_user(uint8_t index, bool active) {
    if (index == 0) {
        if (active) {
            set_single_persistent_default_layer(WINBASE);
        } else {
            set_single_persistent_default_layer(MACBASE);
        }
    }
    if(index == 1) { // Bluetooth switch
        if (active) {
            // Switch to Bluetooth mode: You can add any specific actions needed for Bluetooth mode here
        } else {
            // Switch to wired mode: You can add any specific actions needed for wired mode here
        }
    }
    if(index == 2) { // 2.4GHz switch
        if (active) {
            // Switch to 2.4GHz mode: You can add any specific actions needed for 2.4GHz mode here
        } else {
            // Switch to wired mode: You can add any specific actions needed for the other mode here
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