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
// static painter_image_handle_t qp_image;
// static painter_font_handle_t qp_font;


#define MODS_SHIFT ((get_mods() | get_oneshot_mods()) & MOD_MASK_SHIFT)
#define MODS_CTRL ((get_mods() | get_oneshot_mods()) & MOD_MASK_CTRL)
#define MODS_ALT ((get_mods() | get_oneshot_mods()) & MOD_MASK_ALT)
#define MODS_GUI ((get_mods() | get_oneshot_mods()) & MOD_MASK_GUI)


/* shared styles */
lv_style_t style_screen;
lv_style_t style_container;
lv_style_t style_button;
lv_style_t style_button_active;

/* screens */
static lv_obj_t *screen_home;

// /* home screen content */
static lv_obj_t *label_shift;
static lv_obj_t *label_ctrl;
static lv_obj_t *label_alt;
static lv_obj_t *label_gui;
static lv_obj_t *label_caps;

lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_style_t *style, lv_style_t *style_pressed) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_add_style(label, style, 0);
    lv_obj_add_style(label, style_pressed, LV_STATE_PRESSED);
    return label;
}

void use_flex_row(void *obj) {
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

void use_flex_column(void *obj) {
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

void toggle_state(void *obj, lv_state_t state, bool enabled) {
    if (enabled) {
        lv_obj_add_state(obj, state);
    } else {
        lv_obj_clear_state(obj, state);
    }
}

void init_styles(void) {
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_black());

    lv_style_init(&style_container);
    lv_style_set_pad_top(&style_container, 0);
    lv_style_set_pad_bottom(&style_container, 0);
    lv_style_set_pad_left(&style_container, 0);
    lv_style_set_pad_right(&style_container, 0);
    lv_style_set_bg_opa(&style_container, 0);
    lv_style_set_border_width(&style_container, 0);
    lv_style_set_width(&style_container, lv_pct(100));
    lv_style_set_height(&style_container, LV_SIZE_CONTENT);

    lv_style_init(&style_button);
    lv_style_set_pad_top(&style_button, 4);
    lv_style_set_pad_bottom(&style_button, 4);
    lv_style_set_pad_left(&style_button, 4);
    lv_style_set_pad_right(&style_button, 4);
    lv_style_set_radius(&style_button, 6);
    lv_style_set_text_color(&style_button, lv_palette_main(LV_PALETTE_AMBER));

    lv_style_init(&style_button_active);
    lv_style_set_bg_color(&style_button_active, lv_palette_main(LV_PALETTE_AMBER));
    lv_style_set_bg_opa(&style_button_active, LV_OPA_100);
    lv_style_set_text_color(&style_button_active, lv_color_black());
}

void init_screen_home(void) {
    screen_home = lv_scr_act();

    lv_obj_add_style(screen_home, &style_screen, 0);
    use_flex_column(screen_home);

    lv_obj_t *mods = lv_obj_create(screen_home);
    lv_obj_add_style(mods, &style_container, 0);
    use_flex_column(mods);

    lv_obj_t *mods_row1 = lv_obj_create(mods);
    lv_obj_add_style(mods_row1, &style_container, 0);
    use_flex_row(mods_row1);
    label_gui = create_button(mods_row1, "GUI", &style_button, &style_button_active);
    label_alt = create_button(mods_row1, "ALT", &style_button, &style_button_active);

    lv_obj_t *mods_row2 = lv_obj_create(mods);
    lv_obj_add_style(mods_row2, &style_container, 0);
    use_flex_row(mods_row2);
    label_ctrl  = create_button(mods_row2, "CTL", &style_button, &style_button_active);
    label_shift = create_button(mods_row2, "SFT", &style_button, &style_button_active);

    lv_obj_t *label_brand = lv_label_create(screen_home);
    lv_label_set_text(label_brand, "ajazz ak820 pro");
#if LV_FONT_MONTSERRAT_48
    lv_obj_set_style_text_font(label_brand, &lv_font_montserrat_48, LV_PART_MAIN);
#endif

    label_caps = create_button(screen_home, "CAPS", &style_button, &style_button_active);
}

bool display_backlight_init(void) {
    gpio_set_pin_output(PANEL_BKL);
    gpio_write_pin_high(PANEL_BKL);  // restore this
    return true;
}

bool display_init_kb(void) {

    qp_display = qp_gc9107_make_spi_device(
        PANEL_WIDTH, PANEL_HEIGHT,
        PANEL_CS, PANEL_DC, PANEL_RST,
        4, 3
    );

    qp_set_viewport_offsets(qp_display, LCD_OFFSET_X, LCD_OFFSET_Y);
    qp_init(qp_display, QP_ROTATION_270);

    display_backlight_init();

    qp_lvgl_attach(qp_display);

    lv_disp_t  *lv_display = lv_disp_get_default();
    lv_theme_t *lv_theme   = lv_theme_default_init(lv_display, lv_palette_main(LV_PALETTE_AMBER), lv_palette_main(LV_PALETTE_BLUE), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(lv_display, lv_theme);
    
    init_styles();

    bool res = display_init_user();
    if(res) {
        init_screen_home();
    }

    // gpio_write_pin_low(A16);  // test point 5
    // wait_ms(2000);

    return true;
}

__attribute__((weak)) bool display_init_user(void) {
    return true;
}

__attribute__((weak)) void display_housekeeping_task(void) {
    dprint("display_housekeeping_task_kb\n");

    toggle_state(label_shift, LV_STATE_PRESSED, MODS_SHIFT);
    toggle_state(label_ctrl, LV_STATE_PRESSED, MODS_CTRL);
    toggle_state(label_alt, LV_STATE_PRESSED, MODS_ALT);
    toggle_state(label_gui, LV_STATE_PRESSED, MODS_GUI);
}

__attribute__((weak)) void display_process_caps(bool active) {
    toggle_state(label_caps, LV_STATE_PRESSED, active);
}

void display_suspend(void) {
    qp_lvgl_detach();
    qp_power(qp_display, false);
    gpio_set_pin_output(A16);
    gpio_write_pin_low(A16);
    // Does it go off here even briefly?
    wait_ms(500);
    gpio_write_pin_low(A16);  // write again after 500ms
}


void display_wakeup(void) {
    gpio_set_pin_output(A16);    // force reclaim again
    gpio_write_pin_high(A16);
    qp_power(qp_display, true);
    qp_lvgl_attach(qp_display);
}