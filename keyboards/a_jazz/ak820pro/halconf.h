#pragma once

#define HAL_USE_PAL TRUE
#define HAL_USE_SPI TRUE
#define HAL_USE_SERIAL TRUE

/* The CH582F RX stream shares the main loop with slow display SPI flushes that
 * block for tens of ms. The default 16-byte serial input queue overflows during
 * a flush and the UART drops bytes, shredding the stream (the 5B 32 connect and
 * 5C battery frames get lost). A larger queue buffers across the flush gap. */
#define SERIAL_BUFFERS_SIZE 256

//#define HAL_USE_GPT TRUE

#include_next <halconf.h>