#pragma once

#include_next <mcuconf.h>

#undef SN32_SPI_USE_SPI0
#define SN32_SPI_USE_SPI0 TRUE

#undef SN32_SERIAL_USE_UART2
#define SN32_SERIAL_USE_UART2 TRUE

//#define SN32_SERIAL_UART2_PRIORITY  1


//#define SN32_GPT_USE_CT16B4                 TRUE


