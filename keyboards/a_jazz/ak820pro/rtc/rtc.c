// rtc.c
// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#include "rtc.h"
#include "quantum.h"
#include "hal.h"

#include <time.h>


#ifndef RTC_SCL_PIN
#    define RTC_SCL_PIN A14
#endif

#ifndef RTC_SDA_PIN
#    define RTC_SDA_PIN A15
#endif


#define PCF8563_ADDR            0x51
#define PCF8563_REG_SECONDS     0x02
#define PCF8563_VL_FLAG         0x80
#define PCF8563_I2C_TIMEOUT     TIME_MS2I(20)


#ifndef PCF8563_I2C_DELAY_NOPS
#    define PCF8563_I2C_DELAY_NOPS 15
#endif


static void rtc_i2c_delay(void)
{

    for (volatile uint32_t i = 0;
         i < PCF8563_I2C_DELAY_NOPS;
         i++) {
        __asm__ volatile("nop");
    }

}


static const I2CConfig i2ccfg = {
    .addr10 = false,
    .scl    = RTC_SCL_PIN,
    .sda    = RTC_SDA_PIN,
    .delay  = rtc_i2c_delay,
};



static inline uint8_t bcd2dec(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}


static inline uint8_t dec2bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}


/*
 * ============================================================================
 * PCF8563 reference RTC
 * ============================================================================
 */


static bool pcf_read(rtc_time_t *out)
{
    uint8_t reg = PCF8563_REG_SECONDS;
    uint8_t buf[7];


    if (i2cMasterTransmitTimeout(&I2CD1,
                                 PCF8563_ADDR,
                                 &reg,
                                 1,
                                 buf,
                                 sizeof(buf),
                                 PCF8563_I2C_TIMEOUT) != MSG_OK) {
        return false;
    }


    if (buf[0] & PCF8563_VL_FLAG) {
        return false;
    }


    out->seconds = bcd2dec(buf[0] & 0x7F);
    out->minutes = bcd2dec(buf[1] & 0x7F);
    out->hours   = bcd2dec(buf[2] & 0x3F);
    out->day     = bcd2dec(buf[3] & 0x3F);
    out->weekday = buf[4] & 0x07;
    out->month   = bcd2dec(buf[5] & 0x1F);
    out->year    = 2000U + bcd2dec(buf[6]);


    if (out->seconds > 59 ||
        out->minutes > 59 ||
        out->hours > 23 ||
        out->day < 1 ||
        out->day > 31 ||
        out->month < 1 ||
        out->month > 12) {
        return false;
    }


    return true;
}



static bool pcf_write(const rtc_time_t *t)
{
    uint8_t buf[8] = {
        PCF8563_REG_SECONDS,
        dec2bcd(t->seconds),
        dec2bcd(t->minutes),
        dec2bcd(t->hours),
        dec2bcd(t->day),
        t->weekday & 0x07,
        dec2bcd(t->month),
        dec2bcd((uint8_t)(t->year % 100)),
    };


    return i2cMasterTransmitTimeout(&I2CD1,
                                    PCF8563_ADDR,
                                    buf,
                                    sizeof(buf),
                                    NULL,
                                    0,
                                    PCF8563_I2C_TIMEOUT) == MSG_OK;
}



/*
 * ============================================================================
 * ChibiOS RTC conversion helpers
 * ============================================================================
 */


static void rtc_to_chibiostime(const rtc_time_t *src,
                               RTCDateTime *dst)
{
    dst->year = src->year - RTC_BASE_YEAR;
    dst->month = src->month;
    dst->day = src->day;
    dst->dayofweek = src->weekday;
    dst->dstflag = 0;


    dst->millisecond =
        ((uint32_t)src->hours * 3600UL +
         (uint32_t)src->minutes * 60UL +
         src->seconds) * 1000UL;
}



static void chibiostime_to_rtc(const RTCDateTime *src,
                               rtc_time_t *dst)
{
    dst->year = src->year + RTC_BASE_YEAR;
    dst->month = src->month;
    dst->day = src->day;
    dst->weekday = src->dayofweek;


    dst->hours =
        (uint8_t)(src->millisecond / 3600000UL);

    dst->minutes =
        (uint8_t)((src->millisecond / 60000UL) % 60);

    dst->seconds =
        (uint8_t)((src->millisecond / 1000UL) % 60);
}


/*
 * ============================================================================
 * Synchronization
 * ============================================================================
 */


static bool rtc_valid;


#ifdef RTC_AUTO_CALIBRATION

#ifndef RTC_CHECK_INTERVAL_S
#    define RTC_CHECK_INTERVAL_S 3600
#endif


#ifndef RTC_DRIFT_THRESHOLD_S
#    define RTC_DRIFT_THRESHOLD_S 2
#endif

static volatile uint32_t rtc_check_seconds;

#endif



// Free-running count of RTC second interrupts (~seconds since rtc_init). A cheap
// once-per-second edge source (no localtime()) for pacing the display refresh.
static volatile uint32_t rtc_seconds_count = 0;

static void rtc_second_cb(RTCDriver *rtcp, rtcevent_t event)
{
    (void)rtcp;
    (void)event;

    rtc_seconds_count++;

#ifdef RTC_AUTO_CALIBRATION
    rtc_check_seconds = MIN(rtc_check_seconds + 1, RTC_CHECK_INTERVAL_S);
#endif
}

uint32_t rtc_get_seconds(void) {
    return rtc_seconds_count;
}


static void rtc_seed_from_pcf(void)
{
    rtc_time_t pcf;
    RTCDateTime dt;

    if (!pcf_read(&pcf)) {
        return;
    }

    rtc_to_chibiostime(&pcf, &dt);
    rtcSetTime(&RTCD1,&dt);

    rtc_valid = true;
}

/*
 * ============================================================================
 * Reference check
 * ============================================================================
 */


#ifdef RTC_AUTO_CALIBRATION


static void rtc_clock_discipline(void)
{
    rtc_time_t reference;
    RTCDateTime current;
    RTCDateTime target;

    /*
     * PCF8563 is only accessed during a reference check.
     */
    if (!pcf_read(&reference)) {
        return;
    }

    rtcGetTime(&RTCD1, &current);
    rtc_to_chibiostime(&reference, &target);

    struct tm ref_tm;
    struct tm cur_tm;

    rtcConvertDateTimeToStructTm(&target, &ref_tm, NULL);
    rtcConvertDateTimeToStructTm(&current, &cur_tm, NULL);

    int32_t error = (int32_t)(mktime(&ref_tm) - mktime(&cur_tm));

    /*
     * Only correct when the SN32 RTC has actually drifted.
     */
    if ((error > RTC_DRIFT_THRESHOLD_S) || (error < -RTC_DRIFT_THRESHOLD_S)) {
        rtcSetTime(&RTCD1, &target);
        printf("[rtc] corrected drift of %ld seconds\n", (long)error);
    }
}


#endif

/*
 * ============================================================================
 * Public API
 * ============================================================================
 */


void rtc_init(void)
{
    i2cStart(&I2CD1, &i2ccfg);
    rtcSetCallback(&RTCD1, rtc_second_cb);
    rtc_seed_from_pcf();
}



bool rtc_get_time(rtc_time_t *out)
{
    RTCDateTime dt;

    if (!rtc_valid) {
        return false;
    }

    rtcGetTime(&RTCD1, &dt);
    chibiostime_to_rtc(&dt, out);

    return true;
}



bool rtc_set_time(const rtc_time_t *t)
{
    RTCDateTime dt;

    rtc_to_chibiostime(t, &dt);

    bool ok = pcf_write(t);

    rtcSetTime(&RTCD1, &dt);
    rtc_valid = true;

    return ok;
}



void rtc_task(void)
{
#ifdef RTC_AUTO_CALIBRATION

    if (rtc_check_seconds < RTC_CHECK_INTERVAL_S) {
        return;
    }

    rtc_check_seconds = 0;
    rtc_clock_discipline();
#endif
}