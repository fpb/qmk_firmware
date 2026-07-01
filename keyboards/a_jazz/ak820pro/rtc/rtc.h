// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Time/date read from the external PCF8563-compatible RTC (CHMC D8563F).
 * All fields are plain decimal (already BCD-decoded). */
typedef struct {
    uint8_t  seconds;  // 0-59
    uint8_t  minutes;  // 0-59
    uint8_t  hours;    // 0-23
    uint8_t  day;      // 1-31
    uint8_t  weekday;  // 0-6
    uint8_t  month;    // 1-12
    uint16_t year;     // e.g. 2026
} rtc_time_t;

/* Release both I2C lines (idle high). Safe to call once at startup. */
void rtc_init(void);

/* Read the current time. Returns false on a bus NACK or if the RTC reports a
 * low-voltage / clock-integrity loss (VL bit) -- in that case *out is left
 * untouched and the clock should be considered unset. */
bool rtc_read_time(rtc_time_t *out);

/* Read the raw RTC registers. Returns false only on a bus NACK (no device).
 * Unlike rtc_read_time(), this does NOT reject on the VL flag or out-of-range
 * fields -- *out reflects exactly what the chip holds. Useful for debugging /
 * for surfacing an unset RTC instead of silently hiding it. */
bool rtc_read_raw(rtc_time_t *out);

/* Set the RTC time (and clear the VL/clock-integrity flag). Returns false on a
 * bus NACK. weekday/year fields are written as given. */
bool rtc_set_time(const rtc_time_t *t);
