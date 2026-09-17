// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// See nowplaying.h. Wrap/fit/diff for the now-playing view, shared by both backends.

#include "graphics/nowplaying.h"
#include "media/media.h"

#include <string.h>

// Greedy word-wrap into two <=NP_CHARS lines; "..." on the last line if truncated.
static void wrap_title(const char *s, char l1[NP_CHARS + 1], char l2[NP_CHARS + 1]) {
    char *out[2] = { l1, l2 };
    out[0][0] = out[1][0] = 0;
    uint8_t li = 0;
    const char *p = s;
    bool overflow = false;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *w = p;
        while (*p && *p != ' ') p++;
        uint8_t wl = (uint8_t)(p - w);
        uint8_t cl = (uint8_t)strlen(out[li]);
        uint8_t need = cl ? (uint8_t)(cl + 1 + wl) : wl;
        if (need <= NP_CHARS) {
            if (cl) { out[li][cl] = ' '; cl++; }
            memcpy(out[li] + cl, w, wl); out[li][cl + wl] = 0;
        } else if (cl == 0) {                 // word alone longer than a line
            memcpy(out[li], w, NP_CHARS); out[li][NP_CHARS] = 0;
            overflow = true; break;
        } else if (li == 0) {                 // wrap to line 2
            li = 1;
            uint8_t n = wl < NP_CHARS ? wl : NP_CHARS;
            memcpy(out[1], w, n); out[1][n] = 0;
        } else {
            overflow = true; break;           // would need a 3rd line
        }
    }
    while (*p == ' ') p++;
    if (overflow || *p) {                     // truncated -> ellipsis on the last line
        char *L = out[1][0] ? l2 : l1;
        uint8_t n = (uint8_t)strlen(L);
        if (n > NP_CHARS - 3) n = NP_CHARS - 3;
        L[n] = 0; strcat(L, "...");
    }
}

// One line of at most NP_CHARS, ellipsised if longer (for the artist).
static void fit_line(const char *s, char out[NP_CHARS + 1]) {
    uint8_t n = 0;
    while (s[n] && n < NP_CHARS) { out[n] = s[n]; n++; }
    out[n] = 0;
    if (s[n]) { if (n > NP_CHARS - 3) n = NP_CHARS - 3; out[n] = 0; strcat(out, "..."); }
}

// Last committed render (the pixels currently on screen).
static char     last_l1[NP_CHARS + 1], last_l2[NP_CHARS + 1], last_art[NP_CHARS + 1];
static int8_t   last_playing = -1;
static uint16_t last_fill = 0xFFFF;    // 0xFFFF = nothing drawn yet

void np_render(np_render_t *r, bool force, uint16_t bar_w) {
    bool meta = media_take_dirty();    // a real title/artist/state update arrived

    if (force || meta) {
        wrap_title(media_title(), r->l1, r->l2);
        fit_line(media_artist(), r->art);
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
