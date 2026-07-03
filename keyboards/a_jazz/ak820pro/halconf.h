// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define HAL_USE_PAL TRUE
#define HAL_USE_SPI TRUE
#define HAL_USE_SERIAL TRUE

// RGB matrix: software PWM via the SN32 CT16B1 timer (free-running counter +
// periodic ISR). See drivers/led/sn32f2xx.c F290 block.
#define HAL_USE_PWM TRUE

#include_next <halconf.h>