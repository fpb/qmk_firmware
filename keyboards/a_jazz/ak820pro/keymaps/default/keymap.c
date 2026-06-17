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
        _______,    WIN_LOCK,   _______,                                        _______,                            _______,    _______,    _______,     _______,    _______,    _______
    ),
    /* Mac Base
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
void keyboard_pre_init_user(void){
    gpio_set_pin_output(LED_WINLOCK_PIN);
    gpio_set_pin_output(LED_CHARGING_PIN);
}

#include <qp.h>

#include "graphics/qmklogo.qgf.h"
#include "graphics/robotomono20.qff.h"

static painter_device_t qp_display;
static painter_image_handle_t qp_image;
static painter_font_handle_t qp_font;

static inline void charging_led_init(void) {
    gpio_set_pin_input_high(CHARGE_CHRG_PIN);   // input with pull-up
    gpio_set_pin_input_high(CHARGE_STDBY_PIN);  // input with pull-up
}

static inline void charging_led_update(void) {
    static uint16_t last_check = 0;
    if (timer_elapsed(last_check) < 100) return;  // check every 100ms
    last_check = timer_read();

    bool chrg_active  = !gpio_read_pin(CHARGE_CHRG_PIN);
    bool stdby_active =  gpio_read_pin(CHARGE_STDBY_PIN);
    bool is_charging = chrg_active && stdby_active;

    gpio_write_pin(LED_CHARGING_PIN, is_charging);
}



void keyboard_post_init_user(void) {

    charging_led_init();  // Initialize the charging LED
    
    qp_display = qp_gc9107_make_spi_device(
        PANEL_WIDTH, 
        PANEL_HEIGHT, 
        PANEL_CS, 
        PANEL_DC, 
        PANEL_RST, 
        4, //spi_divisor, 
        3  //spi_mode
    );         // Create the display
    

    qp_init(qp_display, QP_ROTATION_270);   // Initialise the display
    qp_rect(qp_display, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, 0, 255, 0, true);
    
    // LCD backlight on
    gpio_set_pin_output(PANEL_BKL);
    gpio_write_pin_high(PANEL_BKL);
    
    qp_font = qp_load_font_mem(font_robotomono20);
    qp_image = qp_load_image_mem(gfx_qmklogo);

    bool drawn = false;

    if(qp_image != NULL) {
        drawn = qp_drawimage(qp_display, 0, 0, qp_image);
        if(drawn) qp_rect(qp_display, 0, 0, PANEL_WIDTH/2, PANEL_HEIGHT/2, 64, 255, 255, true);
        else      qp_rect(qp_display, 0, 0, PANEL_WIDTH/2, PANEL_HEIGHT/2, 0, 255, 255, true);

    }

    for (int i = 0; i < PANEL_WIDTH; ++i) {
        qp_line(qp_display, i, (drawn?64:0), i, (drawn?64:0)+7, i, 255, 255);
    }

    qp_line(qp_display, 0, 0, PANEL_WIDTH-1, PANEL_HEIGHT-1, 0, 0, 255);

    if (qp_font != NULL) {        
        // 3. Draw the string
        // Arguments: device, X-pixel, Y-pixel, font_handle, string
        qp_drawtext(qp_display, 0, 20, qp_font, "Hello QMK!");
    }

    qp_flush(qp_display);
}

// void keyboard_post_init_user(void) {
// }


bool win_lock_active = false;

bool dip_switch_update_user(uint8_t index, bool active) {
    if (index == 0) {
        if (active) {
            set_single_persistent_default_layer(WINBASE);
            win_lock_active = false; // Desativa o Win Lock ao mudar para o modo Windows
            gpio_write_pin_low(LED_WINLOCK_PIN); // Apaga o LED do Win Lock
        } else {
            set_single_persistent_default_layer(MACBASE);
            win_lock_active = false; // Desativa o Win Lock ao mudar para o modo Mac
            gpio_write_pin_low(LED_WINLOCK_PIN); // Apaga o LED do Win Lock
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
        case KC_LGUI:
            if (record->event.pressed) {
                if(win_lock_active) {
                    // Se o Win Lock estiver ativo, bloqueia a tecla Alt
                    return false; // Skip processing this key
                }
            }
            return true; // Process normally if Win Lock is not active
        case WIN_LOCK:
            if (record->event.pressed) {
                // Toggle the state of the Win Lock
                win_lock_active = !win_lock_active;

                if (win_lock_active) {
                //     // Ativa o Win Lock: Acende o LED e bloqueia a tecla Windows
                    gpio_write_pin_high(LED_WINLOCK_PIN); // Acende o LED do Win Lock
                } else {
                //     // Desativa o Win Lock: Apaga o LED e desbloqueia a tecla Windows
                    gpio_write_pin_low(LED_WINLOCK_PIN); // Apaga o LED do Win Lock
                }
            }
            return false;  // Skip all further processing of this key
        default:
            return true;  // Process all other keycodes normally
    }
}

bool encoder_update_user(uint8_t index, bool clockwise) {
    if (index == 0) { // O seu primeiro (e único) encoder
        if (clockwise) {
            tap_code(KC_VOLU); // Roda para a direita -> Aumenta Volume
        } else {
            tap_code(KC_VOLD); // Roda para a esquerda -> Diminui Volume
        }
    }
    return false; // Retorne false para o QMK saber que o input já foi tratado
}

void housekeeping_task_user(void) {
    charging_led_update();
}
