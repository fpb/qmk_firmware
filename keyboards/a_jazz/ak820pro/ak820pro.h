// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

enum ak820pro_keycodes {
    SCR_TOG = SAFE_RANGE,  // toggle LCD backlight
    BT1,                   // Fn+Q: BT slot 1 (BT mode)
    BT2,                   // Fn+W: BT slot 2 (BT mode)
    BT3,                   // Fn+E: BT slot 3 (BT mode)
    BT24G,                 // Fn+R: 2.4G       (2.4G mode)
    BT_PAIR,               // Fn+P long-press: pair (BT/2.4G)
    AK820PRO_SAFE_RANGE
};
