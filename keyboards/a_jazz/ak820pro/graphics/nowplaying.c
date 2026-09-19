// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// See nowplaying.h. Wrap/fit/diff for the now-playing view, shared by both backends.

#include "graphics/nowplaying.h"
#include "media/media.h"

#include <string.h>

// Single-char truncation marker, replacing "..." to reclaim two cells per line.
#ifndef NP_ELLIPSIS
#    define NP_ELLIPSIS ">"
#endif

// Copy a word capitalizing its first letter (CamelCase packing); returns n.
static uint8_t copy_word_cap(char *dst, const char *w, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) dst[i] = w[i];
    if (n && dst[0] >= 'a' && dst[0] <= 'z') dst[0] -= 32;
    return n;
}

// One greedy wrap pass into two <=NP_CHARS lines. collapse=false keeps the text
// as-is (spaces, original case); collapse=true CamelCase-packs it (drop spaces,
// capitalize word-initials) to fit more. Returns true if it overflowed (some
// word did not fit). Wrapping always breaks on word boundaries either way.
static bool wrap_pass(const char *s, char l1[NP_CHARS + 1], char l2[NP_CHARS + 1], bool collapse) {
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
        uint8_t wl  = (uint8_t)(p - w);
        uint8_t cl  = (uint8_t)strlen(out[li]);
        uint8_t sep = (!collapse && cl) ? 1u : 0u;   // keep a space between words unless collapsing
        uint8_t need = (uint8_t)(cl + sep + wl);
        if (need <= NP_CHARS) {
            if (sep) out[li][cl++] = ' ';
            if (collapse) copy_word_cap(out[li] + cl, w, wl);
            else          memcpy(out[li] + cl, w, wl);
            out[li][cl + wl] = 0;
        } else if (cl == 0) {                        // word alone longer than a line
            if (collapse) copy_word_cap(out[li], w, NP_CHARS);
            else          memcpy(out[li], w, NP_CHARS);
            out[li][NP_CHARS] = 0;
            overflow = true; break;
        } else if (li == 0) {                        // start line 2 with this word
            li = 1;
            uint8_t n = wl < NP_CHARS ? wl : NP_CHARS;
            if (collapse) copy_word_cap(out[1], w, n);
            else          memcpy(out[1], w, n);
            out[1][n] = 0;
        } else {
            overflow = true; break;                  // would need a 3rd line
        }
    }
    while (*p == ' ') p++;
    if (*p) overflow = true;                         // words left over
    return overflow;
}

// Title -> two lines. Keep the text as-is (spaces, original case) when it fits;
// only fall back to CamelCase packing when the spaced form would truncate, and
// only then add the NP_ELLIPSIS marker if even that overflows.
static void wrap_title(const char *s, char l1[NP_CHARS + 1], char l2[NP_CHARS + 1]) {
    if (!wrap_pass(s, l1, l2, false)) return;        // fits with spaces: done
    if (!wrap_pass(s, l1, l2, true))  return;        // fits once collapsed: done
    char *L = l2[0] ? l2 : l1;                       // still too long: mark truncation
    uint8_t n = (uint8_t)strlen(L);
    if (n > NP_CHARS - 1) n = NP_CHARS - 1;
    L[n] = 0; strcat(L, NP_ELLIPSIS);
}

// Artist -> one line. Keep it as-is when it fits; else CamelCase-collapse; else
// NP_ELLIPSIS-mark the truncation.
static void fit_line(const char *s, char out[NP_CHARS + 1]) {
    uint8_t len = (uint8_t)strlen(s);
    if (len <= NP_CHARS) { memcpy(out, s, len); out[len] = 0; return; }  // fits as-is

    uint8_t o = 0;
    bool    ws = true, more = false;
    for (const char *p = s; *p; p++) {
        if (*p == ' ') { ws = true; continue; }
        char c = *p;
        if (ws && c >= 'a' && c <= 'z') c -= 32;      // capitalize word-initial
        if (o >= NP_CHARS) { more = true; break; }    // ran out of room, more remains
        out[o++] = c; ws = false;
    }
    out[o] = 0;
    if (more) {
        uint8_t n = o; if (n > NP_CHARS - 1) n = NP_CHARS - 1;
        out[n] = 0; strcat(out, NP_ELLIPSIS);
    }
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
