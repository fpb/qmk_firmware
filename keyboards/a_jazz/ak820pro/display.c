#include "display.h"

#include "qp.h"


#include "graphics/sonixqmk.qgf.h"
#include "graphics/robotomono20.qff.h"

#define LCD_OFFSET_X 1
#define LCD_OFFSET_Y 2

#define PANEL_DC        D14
#define PANEL_CS        B8
#define PANEL_RST       A17
#define PANEL_BKL       A16

#define PANEL_WIDTH     128
#define PANEL_HEIGHT    128

static painter_device_t qp_display;
static painter_image_handle_t qp_image;
static painter_font_handle_t qp_font;

static bool display_powered = true;

void display_control_power(void) {
    static bool last_power = false;

    if(display_powered != last_power) {
        gpio_write_pin(PANEL_BKL, display_powered);
        last_power = display_powered;
    }
}

void display_set_power(bool on) {
    display_powered = on;
}

bool display_get_power(void) {
    return display_powered;
}

void display_toggle_power(void) {
    display_powered = !display_powered;
}

static bool display_backlight_init(void) {
    gpio_set_pin_output(PANEL_BKL);
    return true;
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
        
    qp_font = qp_load_font_mem(font_robotomono20);
    qp_image = qp_load_image_mem(gfx_sonixqmk);

    if(qp_image != NULL) {
        qp_drawimage(qp_display, 0, 0, qp_image);    
    }
    qp_close_image(qp_image);

    if (qp_font != NULL) {        
        int16_t text_width = qp_textwidth(qp_font, "AK820Pro");
        qp_drawtext(qp_display, (PANEL_WIDTH - text_width), 96, qp_font, "AK820Pro");
    }

    qp_flush(qp_display);

    bool res = display_init_user();
    if(res) { // No more display initialization steps, flush the display to ensure everything is drawn
        // setup default state of the display here if needed
    }

    return true;
}

__attribute__((weak)) bool display_init_user(void) {
    return true;
}



void display_housekeeping_task(void) {
    // char tmp[32];
    // snprintf(tmp, sizeof(tmp), "%d %d %u", display_on, needs_update, suspend_cnt);
    // qp_drawtext(qp_display, 0, 80, qp_font, tmp);
}