#include "qp.h"

bool display_init_kb(void);
bool display_init_user(void);
void display_housekeeping_task(void);



void display_set_power(bool on);
bool display_get_power(void);
void display_toggle_power(void);
void display_control_power(void);