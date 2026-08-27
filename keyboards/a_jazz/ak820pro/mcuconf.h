// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include_next <mcuconf.h>

#undef SN32_SPI_USE_SPI0
#define SN32_SPI_USE_SPI0 TRUE
#define SN32_SPI0_FLASH_DMA   /* flash->LCD DMA driver extension (hal_spi_v2_lld) */

/* dualspi WIP: bring SPI1 (external flash) under the ChibiOS SPI driver too.
 * Enabling the instance compiles SN32_SPI1_HANDLER (dormant until spiStart), and
 * lets the flash->LCD DMA extension borrow/restore SPID1's RX-FIFO IRQ. The
 * bare-metal SPI1 flash I/O is being migrated to spiExchange(&SPID1) -- see
 * graphics/lcd_bus.c. */
#undef SN32_SPI_USE_SPI1
#define SN32_SPI_USE_SPI1 TRUE

#undef SN32_SERIAL_USE_UART2
#define SN32_SERIAL_USE_UART2 TRUE

// RGB matrix hardware PWM spreads the 15 columns over three CT16 timers
// (CT16B0/B1/B2) because CT16B1 alone has only 12 PWM channels. See
// docs/HARDWARE_PWM.md for the column->(timer,channel) map and PFPA constants.
// (No SN32_PWM_NO_RESET: hardware PWM wants the period match to auto-reset the
// counter; only the old software-PWM path re-armed the counter manually.)
// The ChibiOS OS-tick free-running counter defaults to CT16B0 -- but hardware PWM
// needs CT16B0 (column C14 can ONLY route to CT16B0.3). Move the tick counter to
// the otherwise-unused CT16B5 so CT16B0 is free for PWM. (The tick interrupt runs
// on the ARM Cortex SysTick regardless; only the free-running counter uses a CT16.)
#undef SN32_ST_USE_TIMER
#define SN32_ST_USE_TIMER SN32_TIM_CT16B5

#undef SN32_PWM_USE_CT16B0
#define SN32_PWM_USE_CT16B0 TRUE
#undef SN32_PWM_USE_CT16B1
#define SN32_PWM_USE_CT16B1 TRUE
#undef SN32_PWM_USE_CT16B2
#define SN32_PWM_USE_CT16B2 TRUE



