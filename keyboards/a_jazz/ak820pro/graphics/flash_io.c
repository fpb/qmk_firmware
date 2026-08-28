// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared external-flash (SPI1) layer, used by both dashboard backends. See
// flash_io.h. SPI1 is a ChibiOS driver instance (SPID1); the flash->LCD DMA
// borrows it via the spiSN32FlashDma* extension (passing &SPID1).
#include "quantum.h"
#include "gpio.h"
#include "flash_io.h"

// The animation player lives in the backend (graphics/lcd_bus*.c); writes refuse
// while it owns the bus.
extern bool anim_active(void);

// 8-bit, mode 0, 24 MHz -- matches the panel and the DMA extension.
static const SPIConfig flashcfg = {
    .ctrl0  = SPI_DATA_LENGTH(8),
    .ctrl1  = SPI_MLSB_MSB | SPI_CPOL_LOW | SPI_CPHA_FALLING,
    .clkdiv = 0,
};

static bool spi1_inited = false;
static void spi1_setup(void) {
    SN_SYS1->AHBCLKEN |= (1u << 15) | (1u << 26);   // EBI+LCD (shared DMA datapath)
    SN_PFPA->SPI_b.MISO1 = 0b01; SN_PFPA->SPI_b.MOSI1 = 0b01;
    SN_PFPA->SPI_b.SCK1 = 0b11;  SN_PFPA->SPI_b.SEL1  = 0b01;
    gpio_set_pin_output(FLASH_CS); gpio_write_pin(FLASH_CS, 1);
    spiStart(&SPID1, &flashcfg);
    SN_SPI1->DFDLY_b.DFETCH_EN = 1;                 // DMA source auto-fetch
    SN_SPI1->CTRL0_b.FRESET = 0b11;
}
void lcd_flash_init(void) { if (!spi1_inited) { spi1_setup(); spi1_inited = true; } }

static bool spi1_xfer(uint8_t out) { spiSend(&SPID1, 1, &out); return true; }
static uint8_t spi1_rw(uint8_t out) { uint8_t in = 0xFF; spiExchange(&SPID1, 1, &out, &in); return in; }
void spi1_raw_byte(uint8_t out) {
    SN_SPI1->DATA = out; uint32_t n = 0;
    while (SN_SPI1->STAT_b.BUSY) { if (++n > 500000u) break; }
    (void)SN_SPI1->DATA;
}

// ---------------------------------------------------------------------------
// External flash WRITE path (Stage D provisioning). Non-blocking w.r.t. the
// device: a command is issued (microseconds) and the chip goes busy on its own
// (page program ~1-3 ms, 4K sector erase 50-300 ms) -- callers poll flash_busy().
// Every SPI-level spin below is bounded.
// ---------------------------------------------------------------------------
#define FLASH_CMD_WREN      0x06
#define FLASH_CMD_RDSR      0x05
#define FLASH_CMD_PAGE_PROG 0x02
#define FLASH_CMD_SEC_ERASE 0x20
#define FLASH_CMD_JEDEC     0x9F
#define FLASH_PAGE          256u
#define FLASH_SECTOR        4096u
#define FLASH_CHIP_SIZE     0x1000000u   // PY25Q128HA, 16MB

static bool flash_unlocked = false;
void flash_set_unlocked(bool on) { flash_unlocked = on; }

// Animation slots seen in stock firmware: V1.13 boot/user, V1.14 boot/user.
static bool in_anim_slot(uint32_t a, uint32_t len) {
    static const uint32_t slots[] = {0x1AA000u, 0x200000u, 0x38B000u, 0x540000u};
    for (uint8_t i = 0; i < 4; i++)
        if (a >= slots[i] && a + len <= slots[i] + 0x100u + 132u * 0x8000u) return true;
    return false;
}

bool flash_writable(uint32_t addr, uint32_t len) {
    if (!len || addr >= FLASH_CHIP_SIZE || len > FLASH_CHIP_SIZE - addr) return false;
    if (addr >= FLASH_ASSET_BASE) return true;            // our region, always
    return flash_unlocked && in_anim_slot(addr, len);     // stock slots, on request
}

static void flash_cmd_addr(uint8_t cmd, uint32_t a) {
    gpio_write_pin(FLASH_CS, 0);
    spi1_xfer(cmd);
    spi1_xfer((a >> 16) & 0xFF); spi1_xfer((a >> 8) & 0xFF); spi1_xfer(a & 0xFF);
}

static uint8_t flash_status(void) {
    gpio_write_pin(FLASH_CS, 0);
    spi1_xfer(FLASH_CMD_RDSR);
    uint8_t s = spi1_rw(0xFF);
    gpio_write_pin(FLASH_CS, 1);
    return s;
}

bool flash_busy(void) { lcd_flash_init(); return (flash_status() & 0x01) != 0; }

static void flash_wren(void) {
    gpio_write_pin(FLASH_CS, 0); spi1_xfer(FLASH_CMD_WREN); gpio_write_pin(FLASH_CS, 1);
}

uint32_t flash_jedec_id(void) {
    lcd_flash_init();
    gpio_write_pin(FLASH_CS, 0);
    spi1_xfer(FLASH_CMD_JEDEC);
    uint32_t id = ((uint32_t)spi1_rw(0xFF) << 16);
    id |= ((uint32_t)spi1_rw(0xFF) << 8); id |= spi1_rw(0xFF);
    gpio_write_pin(FLASH_CS, 1);
    return id;
}

void flash_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len) {
    lcd_flash_init();
    flash_cmd_addr(FLASH_CMD_READ, addr);
    for (uint32_t i = 0; i < len; i++) dst[i] = spi1_rw(0xFF);
    gpio_write_pin(FLASH_CS, 1);
}

bool flash_erase_sector(uint32_t addr) {
    if (anim_active()) return false;                 // SPI1 is shared with the DMA
    addr &= ~(FLASH_SECTOR - 1u);
    if (!flash_writable(addr, FLASH_SECTOR)) return false;
    lcd_flash_init();
    if (flash_busy()) return false;
    flash_wren();
    flash_cmd_addr(FLASH_CMD_SEC_ERASE, addr);
    gpio_write_pin(FLASH_CS, 1);
    return true;
}

bool flash_page_program(uint32_t addr, const uint8_t *src, uint32_t len) {
    if (anim_active()) return false;
    if (!len || len > FLASH_PAGE) return false;
    if ((addr & (FLASH_PAGE - 1u)) + len > FLASH_PAGE) return false;
    if (!flash_writable(addr, len)) return false;
    lcd_flash_init();
    if (flash_busy()) return false;
    flash_wren();
    flash_cmd_addr(FLASH_CMD_PAGE_PROG, addr);
    for (uint32_t i = 0; i < len; i++) spi1_xfer(src[i]);
    gpio_write_pin(FLASH_CS, 1);
    return true;
}

// CRC32 (IEEE, reflected) over a flash range, resuming from a caller-held
// accumulator so a large verify splits across many short calls. Seed 0xFFFFFFFF,
// invert the final result.
uint32_t flash_crc32_acc(uint32_t crc, uint32_t addr, uint32_t len) {
    lcd_flash_init();
    flash_cmd_addr(FLASH_CMD_READ, addr);
    for (uint32_t i = 0; i < len; i++) {
        crc ^= spi1_rw(0xFF);
        for (uint8_t b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
    gpio_write_pin(FLASH_CS, 1);
    return crc;
}

uint32_t flash_crc32(uint32_t addr, uint32_t len) { return ~flash_crc32_acc(0xFFFFFFFFu, addr, len); }
