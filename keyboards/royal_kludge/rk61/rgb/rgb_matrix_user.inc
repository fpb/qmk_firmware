// !!! DO NOT ADD #pragma once !!! //

// Step 1.
// Declare custom effects using the RGB_MATRIX_EFFECT macro
// (note the lack of semicolon after the macro!)
RGB_MATRIX_EFFECT(bt_pairing_effect)

// Step 2.
// Define effects inside the `RGB_MATRIX_CUSTOM_EFFECT_IMPLS` ifdef block
#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

#define BT_PROFILE_KEY_BASE     15

// e.g: A more complex effect, relying on external methods and state, with
// dedicated init and run methods

static uint16_t intensity_value;
extern uint8_t curr_bt_profile;

static void bt_pairing_effect_init(effect_params_t* params) {
  intensity_value = 0;
}
static bool bt_pairing_effect_run(effect_params_t* params) {
  RGB_MATRIX_USE_LIMITS(led_min, led_max);
  for (uint8_t i = led_min; i < led_max; i++) {
    RGB_MATRIX_TEST_LED_FLAGS();
    uint8_t v =  (intensity_value++) >> 2;
    if(i == BT_PROFILE_KEY_BASE+curr_bt_profile)
        rgb_matrix_set_color(i, v, v, v);
    else rgb_matrix_set_color(i, 1, 1, 1);
  }
  return rgb_matrix_check_finished_leds(led_max);
}
static bool bt_pairing_effect(effect_params_t* params) {
  if (params->init) bt_pairing_effect_init(params);
  return bt_pairing_effect_run(params);
}

#endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS