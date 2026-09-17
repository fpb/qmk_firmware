// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// See media.h. Stores host-pushed now-playing state and self-advances elapsed.

#include "media/media.h"

#include "quantum.h"   // timer_read32 / timer_elapsed32
#include <string.h>

// Sub-commands, in data[2] of a 0x12 frame.
enum {
    MC_CLEAR  = 0x00,  // nothing playing -> revert to the clock
    MC_STATE  = 0x01,  // [playing(1)] [elapsed_ms u32 LE] [duration_ms u32 LE]
    MC_TITLE  = 0x02,  // [offset(1)] [bytes...]   (offset 0 restarts the string)
    MC_ARTIST = 0x03,  // [offset(1)] [bytes...]
};

static char     s_title[MEDIA_STR_MAX + 1];
static char     s_artist[MEDIA_STR_MAX + 1];
static bool     s_active;
static bool     s_playing;
static uint32_t s_elapsed_ms;
static uint32_t s_duration_ms;
static uint32_t s_sync_tick;    // timer_read32() at the last MC_STATE
static bool     s_dirty;        // a real host update since the last media_take_dirty()

// Copy an in-order string chunk into buf at off; offset 0 restarts. Raw HID is
// USB and single-stream, so chunks for one field arrive in order.
static void put_chunk(char *buf, uint8_t off, const uint8_t *src, uint8_t n) {
    if (off > MEDIA_STR_MAX) return;
    if (n > (uint8_t)(MEDIA_STR_MAX - off)) n = (uint8_t)(MEDIA_STR_MAX - off);
    if (n) memcpy(buf + off, src, n);
    buf[off + n] = '\0';        // terminate at the end of this chunk
}

void media_hid_command(uint8_t *data, uint8_t length) {
    if (length < 3) return;
    switch (data[2]) {
        case MC_CLEAR:
            s_active = false;
            s_dirty  = true;
            break;
        case MC_STATE:
            if (length < 12) break;
            s_playing     = data[3] != 0;
            s_elapsed_ms  = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                            ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
            s_duration_ms = (uint32_t)data[8] | ((uint32_t)data[9] << 8) |
                            ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
            s_sync_tick   = timer_read32();
            s_active      = true;
            s_dirty       = true;
            break;
        case MC_TITLE:
            if (length < 4) break;
            put_chunk(s_title, data[3], &data[4], (uint8_t)(length - 4));
            s_active = true;
            s_dirty  = true;
            break;
        case MC_ARTIST:
            if (length < 4) break;
            put_chunk(s_artist, data[3], &data[4], (uint8_t)(length - 4));
            s_active = true;
            s_dirty  = true;
            break;
        default:
            break;
    }
}

bool        media_active(void)      { return s_active; }
bool        media_playing(void)     { return s_playing; }
const char *media_title(void)       { return s_title; }
const char *media_artist(void)      { return s_artist; }
uint32_t    media_duration_ms(void) { return s_duration_ms; }

uint32_t media_elapsed_ms(void) {
    if (!s_playing) return s_elapsed_ms;
    uint32_t e = s_elapsed_ms + timer_elapsed32(s_sync_tick);
    if (s_duration_ms && e > s_duration_ms) e = s_duration_ms;
    return e;
}

// Real host updates only (title/artist/state/clear) -- the renderer does a full
// zone repaint on these. The once-per-second progress advance is handled by the
// renderer redrawing just the bar, not here.
bool media_take_dirty(void) {
    bool d = s_dirty;
    s_dirty = false;
    return d;
}
