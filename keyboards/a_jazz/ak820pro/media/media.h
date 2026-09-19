// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Now-playing media state, pushed from a host agent over Raw HID (channel 0x12,
// the same VIA custom-value framing as the clock/flash channels).
//
// The firmware is a DUMB renderer: the host owns all layout. It sends the exact
// text for each of the MEDIA_LINES display lines (already ASCII-folded, wrapped,
// CamelCased/truncated to fit the panel -- the host knows the chars-per-line),
// plus the play/elapsed/duration state for the progress bar. The firmware only
// stores the lines, self-advances elapsed between updates, and draws each line at
// its fixed position (see graphics/display*.c). It does not know what a "title"
// or "artist" is. Keeping layout host-side means restyling needs no reflash.
//
// Raw HID is a USB-only interface, so updates arrive only while the USB cable is
// connected (independent of whether typing routes to USB or Bluetooth). Fully
// wireless (no cable) simply shows no media, which is fine.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MEDIA_CHANNEL  0x12
#define MEDIA_LINES    3         // display lines the host fills (l1, l2, artist)
#define MEDIA_STR_MAX  48        // max bytes kept per line (host trims to fit)

// After this long paused, stop showing now-playing and let the clock return (the
// track is kept, so resuming re-shows it). 0 disables (paused shows forever).
#ifndef MEDIA_PAUSE_REVERT_MS
#    define MEDIA_PAUSE_REVERT_MS (60u * 1000u)
#endif

// Handle one 0x12 frame: [SET_VALUE, MEDIA_CHANNEL, cmd, payload...]. Call only
// for frames already matched to MEDIA_CHANNEL (see raw_hid_receive).
void media_hid_command(uint8_t *data, uint8_t length);

bool        media_active(void);       // media state exists (a line/state received)
bool        media_show(void);         // whether to show now-playing NOW (active,
                                      // and not paused past MEDIA_PAUSE_REVERT_MS)
bool        media_playing(void);      // playing vs paused
const char *media_line(uint8_t i);    // NUL-terminated display line i (<MEDIA_LINES), may be empty
uint32_t    media_elapsed_ms(void);   // self-advanced since the last host update
uint32_t    media_duration_ms(void);

// Returns (and clears) whether a real host update (title/artist/state/clear)
// arrived since the last call, so the renderer does a full now-playing repaint
// only when the metadata changes. The once-per-second progress advance is not
// reported here -- the renderer redraws just the progress bar from
// media_elapsed_ms() on its own tick.
bool        media_take_dirty(void);
