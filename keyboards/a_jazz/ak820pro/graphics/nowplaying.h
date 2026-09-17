// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Backend-agnostic now-playing render model. Both dashboard backends (custom
// flash-tiles and Quantum Painter) call np_render() to get the current title
// lines / artist / progress and a bitmask of what CHANGED since the last render,
// then draw only the flagged parts with their own primitives. The diff/wrap/fit
// logic lives here once, so the two backends can't drift.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define NP_CHARS   12   // Iosevka-Medium-20 chars per 128px line
#define NP_LINE_H  23   // Iosevka-Medium-20 cell height

// Change flags in np_render_t.changed.
enum {
    NP_CH_L1  = 1u << 0,   // title line 1 changed
    NP_CH_L2  = 1u << 1,   // title line 2 changed
    NP_CH_ART = 1u << 2,   // artist line changed
    NP_CH_BAR = 1u << 3,   // progress bar needs a draw
};

typedef struct {
    char     l1[NP_CHARS + 1];   // wrapped title, line 1
    char     l2[NP_CHARS + 1];   // wrapped title, line 2 ("" if one line)
    char     art[NP_CHARS + 1];  // artist, ellipsised to fit
    bool     playing;            // playing vs paused (for the bar colour)
    uint16_t fill;               // progress fill width in px [0..bar_w]
    uint16_t prev_fill;          // previous fill (for incremental extend)
    bool     bar_full;           // draw the whole bar (force/seek/pause) vs extend
    uint8_t  changed;            // OR of NP_CH_*; 0 = nothing to draw
} np_render_t;

// Compute the render for a bar of bar_w px, diff it against the last committed
// render, and commit. force -> flag everything (used on a full dashboard repaint).
void np_render(np_render_t *r, bool force, uint16_t bar_w);
