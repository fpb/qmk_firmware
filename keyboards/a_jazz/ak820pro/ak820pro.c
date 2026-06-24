#include "ak820pro.h"

#include "gpio.h"

#include "display.h"

void early_hardware_init_post(void) {
    SN_PFPA->SPI_b.MISO0 = 0b11;
    SN_PFPA->SPI_b.MOSI0 = 0b11;
    SN_PFPA->SPI_b.SCK0  = 0b11;
    SN_PFPA->SPI_b.SEL0  = 0b10;
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
 }

 bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    // Let keymap handle layer logic
    if(!process_record_user(keycode, record)) {
        return false;
    }
    switch(keycode) {
        case SCR_TOG:
            if (record->event.pressed)
                display_toggle_power();
            return false;  // Skip all further processing of this key
        default:
            return true;
    }
    return true;
 }

 bool dip_switch_update_kb(uint8_t index, bool active) {
    // Let keymap handle layer logic
    if(!dip_switch_update_user(index, active)) {
        return false;
    }

    // Handle dashboard updates
    if (index == 0) {  // Mac/Windows switch
        if (active) display_draw_windows_logo();
        else        display_draw_mac_logo();
    }
    if(index == 1) {    // Bluetooth switch
        if (active) display_draw_bluetooth_logo();
        else        display_draw_usb_logo();
    }
    if(index == 2) {    // 2.4GHz switch
        if (active) display_draw_2_4_g_logo();
        else        display_draw_usb_logo();
    }
    return true;
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
}

