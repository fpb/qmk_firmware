// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

// Software (bit-banged) I2C master + PCF8563 driver for the AK820Pro's external
// RTC (CHMC D8563F, a PCF8563 clone). The RTC is wired to P0.14 (SCL) and P0.15
// (SDA) -- pins the SN32F290 hardware I2C peripheral cannot reach (see datasheet
// PFPA_I2C), which is why bit-banging is required.

#include "rtc.h"
#include "quantum.h"
#include "gpio.h"

#ifndef RTC_SCL_PIN
#    define RTC_SCL_PIN A14
#endif
#ifndef RTC_SDA_PIN
#    define RTC_SDA_PIN A15
#endif

// Half bit-period. ~5us -> ~100 kHz, well within PCF8563's 400 kHz max and
// forgiving of the board's pull-up strength.
#ifndef RTC_I2C_HALF_US
#    define RTC_I2C_HALF_US 5
#endif

#define PCF8563_ADDR_W 0xA2 // 7-bit 0x51, R/W=0
#define PCF8563_ADDR_R 0xA3 // 7-bit 0x51, R/W=1
#define PCF8563_REG_VL_SECONDS 0x02 // first time register
#define PCF8563_VL_FLAG 0x80        // bit7 of seconds: clock-integrity lost

// --- Open-drain line control: "high" = release to input (external pull-up),
//     "low" = actively drive 0. -------------------------------------------------
static inline void scl_high(void) { gpio_set_pin_input_high(RTC_SCL_PIN); }
static inline void scl_low(void)  { gpio_write_pin_low(RTC_SCL_PIN); gpio_set_pin_output(RTC_SCL_PIN); }
static inline void sda_high(void) { gpio_set_pin_input_high(RTC_SDA_PIN); }
static inline void sda_low(void)  { gpio_write_pin_low(RTC_SDA_PIN); gpio_set_pin_output(RTC_SDA_PIN); }
static inline void i2c_delay(void) { wait_us(RTC_I2C_HALF_US); }

static void i2c_start(void) {
    sda_high();
    scl_high();
    i2c_delay();
    sda_low();
    i2c_delay();
    scl_low();
}

static void i2c_stop(void) {
    sda_low();
    i2c_delay();
    scl_high();
    i2c_delay();
    sda_high();
    i2c_delay();
}

// Write a byte; return true if the slave ACKed.
static bool i2c_write(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x80) sda_high(); else sda_low(); // set data while SCL low
        b <<= 1;
        i2c_delay();
        scl_high();   // slave samples on the rising edge
        i2c_delay();
        scl_low();
    }
    // 9th clock: release SDA, then sample the ACK during the SCL-high phase
    // (SDA pulled low by the slave = ACK).
    sda_high();
    i2c_delay();
    scl_high();
    i2c_delay();
    bool ack = !gpio_read_pin(RTC_SDA_PIN);
    scl_low();
    return ack;
}

// Read a byte; send ACK to continue or NACK to end the read.
static uint8_t i2c_read(bool ack) {
    uint8_t b = 0;
    sda_high(); // release so the slave can drive
    for (uint8_t i = 0; i < 8; i++) {
        i2c_delay();
        scl_high();
        i2c_delay();
        b = (uint8_t)((b << 1) | (gpio_read_pin(RTC_SDA_PIN) ? 1 : 0));
        scl_low();
    }
    // Master ACK (drive low) / NACK (release) during the 9th clock.
    if (ack) sda_low(); else sda_high();
    i2c_delay();
    scl_high();
    i2c_delay();
    scl_low();
    sda_high();
    return b;
}

static inline uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

void rtc_init(void) {
    sda_high();
    scl_high();
}

// Low-level: do the bus transaction and decode the raw registers. Returns false
// only on a bus NACK (no device / wiring fault). On success, *out holds the
// decoded fields exactly as read and *vl (if non-NULL) reflects the VL flag.
static bool rtc_read_regs(rtc_time_t *out, bool *vl) {
    uint8_t buf[7];

    i2c_start();
    if (!i2c_write(PCF8563_ADDR_W))         { i2c_stop(); return false; }
    if (!i2c_write(PCF8563_REG_VL_SECONDS)) { i2c_stop(); return false; }
    i2c_start(); // repeated start
    if (!i2c_write(PCF8563_ADDR_R))         { i2c_stop(); return false; }
    for (uint8_t i = 0; i < 7; i++) {
        buf[i] = i2c_read(i < 6); // ACK all but the last
    }
    i2c_stop();

    if (vl) *vl = (buf[0] & PCF8563_VL_FLAG) != 0;
    out->seconds = bcd2dec(buf[0] & 0x7F);
    out->minutes = bcd2dec(buf[1] & 0x7F);
    out->hours   = bcd2dec(buf[2] & 0x3F);
    out->day     = bcd2dec(buf[3] & 0x3F);
    out->weekday = (uint8_t)(buf[4] & 0x07);
    out->month   = bcd2dec(buf[5] & 0x1F);
    out->year    = (uint16_t)(2000 + bcd2dec(buf[6]));
    return true;
}

bool rtc_read_raw(rtc_time_t *out) {
    return rtc_read_regs(out, NULL);
}

bool rtc_read_time(rtc_time_t *out) {
    rtc_time_t t;
    bool       vl = false;
    if (!rtc_read_regs(&t, &vl)) return false; // bus NACK -> no device
    if (vl) return false;                       // clock integrity lost -> unset

    // Reject implausible reads (bad BCD or a flaky bus) so the caller keeps its
    // previous time / uptime fallback instead of seeding garbage.
    if (t.seconds > 59 || t.minutes > 59 || t.hours > 23 ||
        t.day < 1 || t.day > 31 || t.month < 1 || t.month > 12) {
        return false;
    }

    *out = t;
    return true;
}

bool rtc_set_time(const rtc_time_t *t) {
    i2c_start();
    if (!i2c_write(PCF8563_ADDR_W))         { i2c_stop(); return false; }
    if (!i2c_write(PCF8563_REG_VL_SECONDS)) { i2c_stop(); return false; }
    // Writing seconds with bit7=0 also clears the VL flag.
    if (!i2c_write(dec2bcd(t->seconds) & 0x7F)) { i2c_stop(); return false; }
    if (!i2c_write(dec2bcd(t->minutes)))        { i2c_stop(); return false; }
    if (!i2c_write(dec2bcd(t->hours)))          { i2c_stop(); return false; }
    if (!i2c_write(dec2bcd(t->day)))            { i2c_stop(); return false; }
    if (!i2c_write((uint8_t)(t->weekday & 0x07))) { i2c_stop(); return false; }
    if (!i2c_write(dec2bcd(t->month)))          { i2c_stop(); return false; }
    if (!i2c_write(dec2bcd((uint8_t)(t->year % 100)))) { i2c_stop(); return false; }
    i2c_stop();
    return true;
}
