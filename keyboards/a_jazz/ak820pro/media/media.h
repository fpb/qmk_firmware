// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Now-playing media state, pushed from a host agent over Raw HID (channel 0x12,
// the same VIA custom-value framing as the clock/flash channels). The firmware
// only STORES what it is given and self-advances the elapsed time between
// updates; the dashboard renders it below the status line while media_active()
// (see graphics/display*.c). No scrolling in the firmware -- the host elides and
// wraps titles to fit; a marquee is a possible future add-on.
//
// Raw HID is a USB-only interface, so updates arrive only while the USB cable is
// connected (independent of whether typing routes to USB or Bluetooth). Fully
// wireless (no cable) simply shows no media, which is fine.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MEDIA_CHANNEL  0x12
#define MEDIA_STR_MAX  48        // max title/artist bytes kept (host pre-trims)

// Handle one 0x12 frame: [SET_VALUE, MEDIA_CHANNEL, cmd, payload...]. Call only
// for frames already matched to MEDIA_CHANNEL (see raw_hid_receive).
void media_hid_command(uint8_t *data, uint8_t length);

bool        media_active(void);       // something to show (else the clock shows)
bool        media_playing(void);      // playing vs paused
const char *media_title(void);        // NUL-terminated, may be empty
const char *media_artist(void);
uint32_t    media_elapsed_ms(void);   // self-advanced since the last host update
uint32_t    media_duration_ms(void);

// Returns (and clears) whether a real host update (title/artist/state/clear)
// arrived since the last call, so the renderer does a full now-playing repaint
// only when the metadata changes. The once-per-second progress advance is not
// reported here -- the renderer redraws just the progress bar from
// media_elapsed_ms() on its own tick.
bool        media_take_dirty(void);
