// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// See nowplaying.h. Diff model for the now-playing view, shared by both backends.
// The host owns all layout: it sends the exact text of each of the three display
// lines (media_line 0/1/2), already folded/wrapped/truncated to fit. This file
// just copies those lines, diffs them against what's on screen, and reports what
// changed. No wrapping/CamelCase/ellipsis here -- that all lives in the host agent.

#include "graphics/nowplaying.h"
#include "media/media.h"

#include <string.h>

// Copy a host line into an NP_CHARS cell, truncating defensively (the host trims
// to the panel width, but never trust the wire not to overrun the buffer).
static void copy_line(char dst[NP_CHARS + 1], const char *src) {
    uint8_t n = 0;
    while (src[n] && n < NP_CHARS) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

// Last committed render (the pixels currently on screen).
static char     last_l1[NP_CHARS + 1], last_l2[NP_CHARS + 1], last_art[NP_CHARS + 1];
static int8_t   last_playing = -1;
static uint16_t last_fill = 0xFFFF;    // 0xFFFF = nothing drawn yet

// Invalidate the "what's on screen" cache so the next np_render() repaints every
// field. MUST be called whenever the now-playing pixels are cleared by something
// other than np_render itself (a view switch, a full dashboard repaint, sleep,
// the animation player) -- otherwise last_* outlives the pixels and a later
// same-value update is diffed away, leaving the field blank until a reboot. The
// sentinels are strings no real field can equal ('\1'), plus the "nothing drawn"
// markers for the bar/state.
void np_invalidate(void) {
    last_l1[0] = last_l2[0] = last_art[0] = '\1';
    last_l1[1] = last_l2[1] = last_art[1] = '\0';
    last_playing = -1;
    last_fill    = 0xFFFF;
}

void np_render(np_render_t *r, bool force, uint16_t bar_w) {
    bool meta = media_take_dirty();    // a real title/artist/state update arrived

    if (force || meta) {                // host pushed new line text
        copy_line(r->l1,  media_line(0));
        copy_line(r->l2,  media_line(1));
        copy_line(r->art, media_line(2));
    } else {                            // reuse -- only progress can have moved
        strcpy(r->l1, last_l1); strcpy(r->l2, last_l2); strcpy(r->art, last_art);
    }
    r->playing = media_playing();

    uint32_t dur = media_duration_ms(), el = media_elapsed_ms();
    uint16_t fill = dur ? (uint16_t)((uint32_t)bar_w * el / dur) : 0;
    if (fill > bar_w) fill = bar_w;
    r->fill      = fill;
    r->prev_fill = (last_fill == 0xFFFF) ? 0 : last_fill;

    bool play_changed = (int8_t)r->playing != last_playing;
    uint8_t ch = 0;
    if (force || strcmp(r->l1,  last_l1))  ch |= NP_CH_L1;
    if (force || strcmp(r->l2,  last_l2))  ch |= NP_CH_L2;
    if (force || strcmp(r->art, last_art)) ch |= NP_CH_ART;
    if (force || play_changed || fill != last_fill) ch |= NP_CH_BAR;
    r->bar_full = force || play_changed || last_fill == 0xFFFF || fill < last_fill;
    r->changed  = ch;

    strcpy(last_l1, r->l1); strcpy(last_l2, r->l2); strcpy(last_art, r->art);
    last_playing = r->playing;
    last_fill    = fill;
}
