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
        4, //spi_divisor, 
        0  //spi_mode
    );         // Create the display
    

    qp_init(qp_display, QP_ROTATION_270);   // Initialise the display
    //qp_rect(qp_display, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, 0, 255, 255, true);
    
    // LCD backlight on
    gpio_set_pin_output(PANEL_BKL);
    gpio_write_pin_high(PANEL_BKL);
    
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
    qp_flush(qp_display);


    return keyboard_post_init_user();
}

void dummy(void) {
    return;
}

__attribute__((weak)) void keyboard_pre_init_user(void);

void keyboard_pre_init_kb(void) {
    
    // 1. Ativa o Clock global de GPIO (Bit 3 do registador AHBCLKEN)
    SN_SYS1->AHBCLKEN |= (1 << 3); 

    // 2. Ativa o Clock do periférico SPI0 (Bit 0 do registador APBCP0)
    SN_SYS1->APBCP0 |= (1 << 0); 

    __asm__ volatile("nop; nop; nop; nop;");

    // 3. Configuração dos pinos do teclado D15
    SN_PFPA->SPI_b.MISO0 = 0b11; 
    SN_PFPA->SPI_b.MOSI0 = 0b11; 
    SN_PFPA->SPI_b.SCK0  = 0b11; 
    SN_PFPA->SPI_b.SEL0  = 0b10;     

    palSetPadMode(GPIOB, 12, PAL_MODE_INPUT_PULLUP);
    palSetPadMode(GPIOB, 13, PAL_MODE_INPUT_PULLUP);

    palSetPadMode(GPIOC, 15, PAL_MODE_OUTPUT_PUSHPULL); // P3.15 -> LED BT
    palSetPadMode(GPIOB, 18, PAL_MODE_OUTPUT_PUSHPULL); // P1.18 -> LED 2.4G

    palClearPad(GPIOC, 15);
    palClearPad(GPIOB, 18);


    // dummy();
    // dummy();
}

uint8_t hardware_boot_mode = 0; // 0 = USB, 1 = BT, 2 = 2.4G

void housekeeping_task_kb(void) {
    // Leitura direta dos buffers dos sliders (sem interrupções)
    bool pin_12_low = (palReadPad(GPIOB, 12) == 0);
    bool pin_13_low = (palReadPad(GPIOB, 13) == 0);

    if (pin_12_low) {
        if (hardware_boot_mode != 1) {
            hardware_boot_mode = 1;
            
            // Modo Bluetooth: Acende o LED do BT (C15) e apaga o outro
            palSetPad(GPIOC, 15);   // Set = HIGH (Acende)
            palClearPad(GPIOB, 18); // Clear = LOW (Apaga)
        }
    } else if (pin_13_low) {
        if (hardware_boot_mode != 2) {
            hardware_boot_mode = 2;
            
            // Modo 2.4Ghz: Acende o LED do 2.4G (B18) e apaga o outro
            palClearPad(GPIOC, 15);
            palSetPad(GPIOB, 18);
        }
    } else {
        if (hardware_boot_mode != 0) {
            hardware_boot_mode = 0;
            
            // Modo USB (Nenhum pino em LOW): Apaga ambos os LEDs
            palClearPad(GPIOC, 15);
            palClearPad(GPIOB, 18);
        }
    }
}
