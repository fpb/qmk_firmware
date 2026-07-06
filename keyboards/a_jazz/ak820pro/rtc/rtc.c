// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

// PCF8563 driver for the AK820Pro's external RTC (CHMC D8563F, a PCF8563 clone)
// wired to P0.14 (SCL) and P0.15 (SDA) -- pins the SN32F290 hardware I2C
// peripheral cannot reach (see datasheet PFPA_I2C). This variant drives them via
// ChibiOS's software (bit-banged) I2C fallback LLD (USE_HAL_I2C_FALLBACK=yes in
// rules.mk) through the standard I2C HAL API.
//
// REQUIRES the local patch in ./fix.diff applied to the vendored fallback driver
// (lib/chibios-contrib/os/hal/lib/fallback/I2C/hal_i2c_lld.c). The stock driver
// does NOT work on this board: in OPENDRAIN=FALSE mode it releases SCL to input
// each clock, and the SN32 reads a just-driven-then-released SCL as low, so every
// transfer times out. The patch keeps SCL push-pull throughout (no clock-stretch,
// which the PCF8563 never uses), idles the pins in i2c_lld_start, and cleans up
// the SDA read/release ordering. With the patch this reads/sets the RTC reliably.
// The zero-dependency alternative is the hand-rolled bit-bang on ak820pro-rgbless.

#include "rtc.h"
#include "quantum.h"
#include "hal.h"

#ifndef RTC_SCL_PIN
#    define RTC_SCL_PIN A14
#endif
#ifndef RTC_SDA_PIN
#    define RTC_SDA_PIN A15
#endif

// Per-transaction timeout. A whole read/write is only a few dozen bit-times, so
// 20 ms is generous even at the slow software clock.
#define RTC_I2C_TIMEOUT TIME_MS2I(20)

#define PCF8563_ADDR           0x51 // 7-bit
#define PCF8563_REG_VL_SECONDS 0x02 // first time register
#define PCF8563_VL_FLAG        0x80 // bit7 of seconds: clock-integrity lost

// Custom half-bit delay: a non-yielding busy-wait of RTC_I2C_DELAY_NOPS nops
// (SW_I2C_USE_OSAL_DELAY == FALSE). Keeps the transfer atomic and lets us tune
// the bit-rate directly in CPU cycles.
#ifndef RTC_I2C_DELAY_NOPS
#    define RTC_I2C_DELAY_NOPS 15
#endif

static void rtc_i2c_delay(void) {
    chSysLock();
    for (volatile uint32_t i = 0; i < RTC_I2C_DELAY_NOPS; i++) {
        __asm__ volatile("nop");
    }
    chSysUnlock();
}

static const I2CConfig i2ccfg = {
    .addr10 = false,
    .scl    = RTC_SCL_PIN,
    .sda    = RTC_SDA_PIN,
    .delay  = rtc_i2c_delay, // SW_I2C_USE_OSAL_DELAY == FALSE -> external delay fn
};

static inline uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

void rtc_init(void) {
    // Called from keyboard_post_init_kb (kernel already running, so the
    // osal-delay in the SW driver is safe).
    i2cStart(&I2CD1, &i2ccfg);
}

// Low-level: read the 7 time registers in a single write-then-read transaction
// (register pointer, repeated start, burst read). Returns false on a bus error
// (no device / wiring fault). On success *out holds the decoded fields and *vl
// (if non-NULL) reflects the VL clock-integrity flag.
static bool rtc_read_regs(rtc_time_t *out, bool *vl) {
    uint8_t reg = PCF8563_REG_VL_SECONDS;
    uint8_t buf[7];

    if (i2cMasterTransmitTimeout(&I2CD1, PCF8563_ADDR, &reg, 1, buf, sizeof(buf),
                                 RTC_I2C_TIMEOUT) != MSG_OK) {
        return false;
    }

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
    if (!rtc_read_regs(&t, &vl)) return false; // bus error -> no device
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
    // Register pointer followed by the 7 time bytes in one write. Writing seconds
    // with bit7 = 0 also clears the VL flag.
    uint8_t buf[8] = {
        PCF8563_REG_VL_SECONDS,
        (uint8_t)(dec2bcd(t->seconds) & 0x7F),
        dec2bcd(t->minutes),
        dec2bcd(t->hours),
        dec2bcd(t->day),
        (uint8_t)(t->weekday & 0x07),
        dec2bcd(t->month),
        dec2bcd((uint8_t)(t->year % 100)),
    };
    return i2cMasterTransmitTimeout(&I2CD1, PCF8563_ADDR, buf, sizeof(buf),
                                    NULL, 0, RTC_I2C_TIMEOUT) == MSG_OK;
}
