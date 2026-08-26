// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define HAL_USE_PAL TRUE
#define HAL_USE_SPI TRUE   /* unified experiment: dashboard CPU pushes via the driver */
#define HAL_USE_SERIAL TRUE
#define HAL_USE_RTC TRUE

// RGB matrix: hardware PWM across the SN32 CT16B0/B1/B2 timers.
// See drivers/led/sn32f2xx.c F290 block + hardware_pwm.diff.
#define HAL_USE_PWM TRUE

// External PCF8563 RTC on P0.14/P0.15 via the ChibiOS software (bit-banged) I2C
// fallback LLD (USE_HAL_I2C_FALLBACK=yes in rules.mk). Requires the i2c_fallback.diff patch
// to the fallback driver (see rtc.c / readme). The SN32 HW I2C peripheral cannot
// reach those pins.
#define HAL_USE_I2C TRUE
#define SW_I2C_USE_I2C1 TRUE        // provides the I2CD1 instance
#define SW_I2C_USE_OPENDRAIN FALSE  // emulate open-drain by input/output switching
#define SW_I2C_USE_OSAL_DELAY FALSE // use rtc.c's busy-wait delay (non-yielding)

#include_next <halconf.h>