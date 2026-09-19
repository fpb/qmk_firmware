// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Backend-agnostic now-playing render model. Both dashboard backends (custom
// flash-tiles and Quantum Painter) call np_render() to get the three display
// lines (their text supplied verbatim by the host) plus the progress state, and
// a bitmask of what CHANGED since the last render, then draw only the flagged
// parts. Layout (wrap/CamelCase/truncate) is the host's job -- this file only
// copies the host lines and diffs them, so the two backends can't drift.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define NP_CHARS   12   // Iosevka-Medium-20 chars per 128px line (defensive cap)
#define NP_LINE_H  23   // Iosevka-Medium-20 cell height

// Change flags in np_render_t.changed. l1/l2 sit at the two upper text rows, art
// at the lower (drawn dimmer); the host decides what text each holds.
enum {
    NP_CH_L1  = 1u << 0,   // line 0 changed
    NP_CH_L2  = 1u << 1,   // line 1 changed
    NP_CH_ART = 1u << 2,   // line 2 changed
    NP_CH_BAR = 1u << 3,   // progress bar needs a draw
};

typedef struct {
    char     l1[NP_CHARS + 1];   // host line 0 (upper text row)
    char     l2[NP_CHARS + 1];   // host line 1 (middle text row)
    char     art[NP_CHARS + 1];  // host line 2 (lower text row, drawn dimmer)
    bool     playing;            // playing vs paused (for the bar colour)
    uint16_t fill;               // progress fill width in px [0..bar_w]
    uint16_t prev_fill;          // previous fill (for incremental extend)
    bool     bar_full;           // draw the whole bar (force/seek/pause) vs extend
    uint8_t  changed;            // OR of NP_CH_*; 0 = nothing to draw
} np_render_t;

// Compute the render for a bar of bar_w px, diff it against the last committed
// render, and commit. force -> flag everything (used on a full dashboard repaint).
void np_render(np_render_t *r, bool force, uint16_t bar_w);

// Drop the "what's on screen" cache so the next np_render() repaints every field.
// Call it whenever the now-playing pixels are cleared outside np_render (view
// switch, full dashboard repaint, sleep, animation) to keep the diff in sync with
// the panel -- otherwise a later same-value update is diffed away and the field
// stays blank until a reboot.
void np_invalidate(void);
