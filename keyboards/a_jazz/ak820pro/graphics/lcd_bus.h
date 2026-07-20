// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bare-metal LCD bus for the AK820 Pro: we own SPI0 (+ its IRQ Vector58) and SPI1
// (flash), so the SPI-to-SPI flash->LCD DMA can be interrupt-driven. The GC9107 panel
// and dashboard are driven entirely bare-metal (no Quantum Painter, no ChibiOS SPI).
// See docs/LCD_FLASH_LAYER.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Bring up the GC9107 panel (reset + init sequence + rotation 270).
void lcd_init(void);

// Flash-animation player (interrupt-driven DMA; pauses the dashboard, borrows the bus).
void anim_toggle(void);
void anim_task(void);
// True while the animation owns the bus; suspend RTC I2C polling then (shared port A).
bool anim_active(void);

// --- Stage C tile primitives (see docs/LCD_FLASH_LAYER.md) -------------------
// The SN32 DMA is SPI-to-SPI ONLY (source = the other SPI's RX FIFO, no source-address
// register), so only FLASH-resident art can be DMA'd. RAM-resident art is CPU-pushed
// through the pipelined bulk writer -- fast (~wire speed) but blocking.

// DMA, non-blocking: flash tile -> panel rect. Poll lcd_blit_busy() for completion.
void lcd_blit_flash(uint32_t src, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
bool lcd_blit_busy(void);

// CPU, blocking: RGB565 tile from a firmware/RAM array -> panel rect.
void lcd_blit_ram(const uint16_t *px, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

// Bare-metal dashboard drawing (Quantum-Painter-free).
void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void lcd_draw_qgf(uint16_t x, uint16_t y, const uint8_t *qgf);   // native RGB565 QGF image
void lcd_draw_text(uint16_t x, uint16_t y, const uint8_t *font, const char *s, uint16_t fg, uint16_t bg);
uint16_t lcd_text_width(const uint8_t *font, const char *s);    // total advance width of a string
