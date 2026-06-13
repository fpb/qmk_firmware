#include "ak820pro.h"

#include "gpio.h"

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
    qp_rect(qp_display, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, 0, 255, 0, true);
    
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

//static bool msg_encoder_last_A = true;

void early_hardware_init_pre(void) {
    SN_PFPA->SPI_b.MISO0 = 0b11; 
    SN_PFPA->SPI_b.MOSI0 = 0b11; 
    SN_PFPA->SPI_b.SCK0  = 0b11; 
    SN_PFPA->SPI_b.SEL0  = 0b10;
}

void keyboard_pre_init_kb(void) {
    
    // 1. Ativa o Clock global de GPIO (Bit 3 do registador AHBCLKEN)
    //SN_SYS1->AHBCLKEN |= (1 << 3); 

    // 2. Ativa o Clock do periférico SPI0 (Bit 0 do registador APBCP0)
    //SN_SYS1->APBCP0 |= (1 << 0); 

    // __asm__ volatile("nop; nop; nop; nop;");

    // SN_PFPA->SPI_b.MISO0 = 0b11; 
    // SN_PFPA->SPI_b.MOSI0 = 0b11; 
    // SN_PFPA->SPI_b.SCK0  = 0b11; 
    // SN_PFPA->SPI_b.SEL0  = 0b10;     

    // Configure BT/2.4G switch
    //palSetPadMode(GPIOB, 12, PAL_MODE_INPUT_PULLUP);
    //palSetPadMode(GPIOB, 13, PAL_MODE_INPUT_PULLUP);

    // Configure Win lock and charing leds as output
    //palSetPadMode(GPIOC, 15, PAL_MODE_OUTPUT_PUSHPULL); // P3.15 -> LED BT
    //palSetPadMode(GPIOB, 18, PAL_MODE_OUTPUT_PUSHPULL); // P1.18 -> LED 2.4G

    // Turn off LEDS
    //palClearPad(GPIOC, 15);
    //palClearPad(GPIOB, 18);

    // Configure rotary encoder pins
//    palSetPadMode(GPIOA, 10, PAL_MODE_INPUT_PULLUP);
//    palSetPadMode(GPIOB, 2,  PAL_MODE_INPUT_PULLUP);


//    msg_encoder_last_A = palReadPad(GPIOA, 10);

    // dummy();
    // dummy();
}

//uint8_t hardware_boot_mode = 0; // 0 = USB, 1 = BT, 2 = 2.4G

void housekeeping_task_kb(void) {
    // Leitura direta dos buffers dos sliders (sem interrupções)
/*    bool pin_12_low = (palReadPad(GPIOB, 12) == 0);
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
*/
/*
    bool current_A = palReadPad(GPIOA, 10);
    bool current_B = palReadPad(GPIOB, 2);

    // Detetar se o pino A mudou de estado (Transição de sinal)
    if (current_A != msg_encoder_last_A) {
        // Se o pino A mudou, o estado do pino B diz-nos a direção
        if (current_A == current_B) {
            // Rotação no sentido dos ponteiros do relógio (Clockwise)
            tap_code(KC_VOLU); // Aumenta Volume
        } else {
            // Rotação no sentido inverso (Counter-Clockwise)
            tap_code(KC_VOLD); // Diminui Volume
        }
    }
    // Atualiza o histórico para o próximo ciclo
    msg_encoder_last_A = current_A;
*/

}

__attribute__((weak)) bool encoder_update_user(uint8_t, bool);

bool encoder_update_kb(uint8_t index, bool clockwise) {
    // if (!encoder_update_user(index, clockwise)) {
    //   return false; /* Don't process further events if user function exists and returns false */
    // }
    
    //palClearPad(GPIOC, 15);

    if (index == 0) { /* First encoder */
        if (clockwise) {
            tap_code(KC_VOLU);
        } else {
            tap_code(KC_VOLD);
        }
    } 
    return true;
}
