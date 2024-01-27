/* Copyright 2022 Philip Mourdjis <philip.j.m@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "quantum.h"

#define RK_BT_PROFILE_NONE  -1
#define RK_BT_PROFILE_1     0
#define RK_BT_PROFILE_2     1
#define RK_BT_PROFILE_3     2
#define RK_BT_PROFILE_4     3
#define RK_BT_PROFILE_5     4

enum rk68_keycodes {
    KC_WIN_KEY = QK_KB_0,
    KC_MAC_KEY,
#ifdef BLUETOOTH_ENABLE
    BT_PROFILE1,
    BT_PROFILE2,
    BT_PROFILE3,
    BT_PROFILE4,
    BT_PROFILE5,
    BT_PAIR,
    BT_TOGGLE,
    BT_RESET,
#endif
    RK68_SAFE_RANGE
};
