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
//#define SN32F2XX_RGB_MATRIX_ROW_PINS { A11, B4, B5, A8, A9, D8, D9, D10, D11, D12, D13, D16, D17, D18, C10, C11, C12, C13 }

/* Configure the effects:                                              */
//#define RGB_MATRIX_TYPING_HEATMAP_DECREASE_DELAY_MS 50

#define SPI_DRIVER SPID0

#define SPI_MOSI_PIN    D2
#define SPI_SCK_PIN     D0
#define SPI_MISO_PIN    NO_PIN
#define SPI_SS_PIN      B8
