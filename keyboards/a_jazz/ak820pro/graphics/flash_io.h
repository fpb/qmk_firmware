// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared external-flash (SPI1) layer for both dashboard backends (dualspi base):
// SPID1 is a ChibiOS driver instance; reads + ak820ctl provisioning go through
// spiExchange(&SPID1). Only the flash->LCD DMA command phase pokes SN_SPI1
// directly (spi1_raw_byte), with the driver vector disabled by the DMA extension.
#pragma once

#include <stdint.h>
#include <stdbool.h>

// Shared pins/commands used by both backends' DMA blit as well as this layer.
#define FLASH_CS          A13
#define FLASH_CMD_READ    0x03
#define FLASH_ASSET_BASE  0x0CE0000u

// Bring SPI1/SPID1 up once (idempotent). Called lazily by the flash ops and by
// the backends before a DMA blit.
void lcd_flash_init(void);

// One raw byte on SPI1 for the DMA READ+addr command phase (driver vector off).
void spi1_raw_byte(uint8_t out);

// Provisioning / read API (used by ak820pro.c's ak820ctl handler and by the
// custom backend's flash-tile renderer). Writes refuse while an animation owns
// the bus; callers poll flash_busy() rather than blocking.
bool     flash_busy(void);
bool     flash_erase_sector(uint32_t addr);                              // 4K
bool     flash_page_program(uint32_t addr, const uint8_t *src, uint32_t len);
void     flash_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len);
uint32_t flash_crc32(uint32_t addr, uint32_t len);
uint32_t flash_crc32_acc(uint32_t crc, uint32_t addr, uint32_t len);
uint32_t flash_jedec_id(void);
bool     flash_writable(uint32_t addr, uint32_t len);
void     flash_set_unlocked(bool on);
