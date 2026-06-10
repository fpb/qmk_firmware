#include "ak820pro.h"

#include "gpio.h"
#define TOSTRING(x) #x
#define STRINGIFY(x) TOSTRING(x)
#pragma message "LED_CAPS_LOCK_PIN = " STRINGIFY(LED_CAPS_LOCK_PIN)
#pragma message "LED_PIN_ON_STATE = " STRINGIFY(LED_PIN_ON_STATE)


#include <qp.h>

#include "graphics/qmklogo.qgf.h"

static painter_device_t qp_display;
static painter_image_handle_t qp_image;

void keyboard_post_init_kb(void) {
    qp_display = qp_gc9107_make_spi_device(
        PANEL_WIDTH, 
        PANEL_HEIGHT, 
        PANEL_CS, 
        PANEL_DC, 
        PANEL_RST, 
        8, //spi_divisor, 
        0  //spi_mode
    );         // Create the display
    qp_init(qp_display, QP_ROTATION_180);   // Initialise the display
    qp_rect(qp_display, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, 128, 255, 255, true);
    
    qp_image = qp_load_image_mem(gfx_qmklogo);
    qp_drawimage(qp_display, 0, 0, qp_image);

    // LCD backlight on
    // gpio_set_pin_output(PANEL_BKL);
    // gpio_write_pin_high(PANEL_BKL);

    return keyboard_post_init_user();
}


__attribute__((weak)) void keyboard_pre_init_user(void);

void keyboard_pre_init_kb(void) {
    gpio_set_pin_output(D15);
    gpio_write_pin_low(D15);



    // Keep this to allow user-level hooks to still fire if defined
    keyboard_pre_init_user();// Call the user hook safely

    keyboard_pre_init_user();
}

