/* Copyright (C) 2023 Benjamín Ausensi <bausensi@protonmail.com>
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

/* RGB matrix configuration can't be fully expressed in JSON as of now */
#define RGB_MATRIX_LED_COUNT 68
#define SN32_RGB_MATRIX_ROW_PINS { C6, C4, C5, C9, C7, C8, C12, C10, C11, B13, C13, C14, B14, D3, B15 }
 
/* Enable additional RGB effects                                       */
#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES

/* Configure the effects:                                              */
#define RGB_MATRIX_TYPING_HEATMAP_DECREASE_DELAY_MS 50

