#include "ak820pro.h"

#include "gpio.h"

void early_hardware_init_pre(void) {
    SN_PFPA->SPI_b.MISO0 = 0b11; 
    SN_PFPA->SPI_b.MOSI0 = 0b11; 
    SN_PFPA->SPI_b.SCK0  = 0b11; 
    SN_PFPA->SPI_b.SEL0  = 0b10;
}