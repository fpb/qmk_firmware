#include "qp.h"
#include "rtc.h"

bool display_init_kb(void);
bool display_init_user(void);
void display_housekeeping_task(void);

void display_set_power(bool on);
bool display_get_power(void);
void display_toggle_power(void);
void display_control_power(void);

// Clock set mode: Fn+Knob steps through day/month/HH/MM/SS, knob rotation
// adjusts the selected field; the RTC is written only on the final step.
void clock_edit_step(void);          // enter / next field / commit + exit
void clock_edit_adjust(int8_t dir);  // +1 / -1 the selected field
bool clock_edit_active(void);
void clock_set(const rtc_time_t *t); // write-through: re-seed the live clock

void display_draw_mac_logo(void);
void display_draw_windows_logo(void);
void display_draw_usb_logo(void);
void display_draw_bluetooth_logo(void);
void display_draw_2_4_g_logo(void);
