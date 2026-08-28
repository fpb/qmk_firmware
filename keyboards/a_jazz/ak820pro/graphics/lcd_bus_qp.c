// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// [ak820pro-flashlcd-qp-lld]
// QP drives the GC9107 through the STOCK ChibiOS SPI0 driver (gc9107_spi). This file
// keeps ONLY the flash SPI1 (bare-metal) + the panel command/window helpers. The
// SPI-to-SPI flash->LCD DMA is a driver extension (spiSN32FlashDma*, in the patched
// hal_spi_v2_lld.c, gated by SN32_SPI0_FLASH_DMA in mcuconf.h): it borrows SPI0 for
// the DMA data phase and services completion on the driver's own SPI0 vector, so this
// file no longer configures SPI0 or hooks the vector. During an animation QP is paused.
//
// This branch tests QP-over-stock-ChibiOS-SPI coexisting with the background DMA,
// unlike QP-over-our-bare-metal-comms which corrupted the top rows. See
// docs/LCD_FLASH_LAYER.md.

#include "quantum.h"
#include "gpio.h"
#include "lcd_bus.h"

extern void display_set_paused(bool paused);   // graphics/display.c

// --- pins --------------------------------------------------------------------
#define PANEL_DC   D14
#define PANEL_CS   B8
#define FLASH_CS   A13

#define FRAME_W 128
#define FRAME_H 128
#define FRAME_BYTES (FRAME_W * FRAME_H * 2)
#define LCD_OFF_X 1
#define LCD_OFF_Y 2
#define FLASH_CMD_READ 0x03

// Mario animation
#define ANIM_BASE   0x540000u
#define ANIM_HDR    0x100u
#define ANIM_STRIDE 0x8000u
#define ANIM_COUNT  8


// SPI0 (LCD) is owned by the ChibiOS SPI driver. The flash->LCD DMA is now a
// driver extension (spiSN32FlashDma*, hal_spi_v2_lld.c): it borrows SPI0 for the
// DMA data phase and services completion on the driver's own SPI0 vector, so
// this file no longer configures SPI0 or hooks the vector for the DMA.

// ---------------------------------------------------------------------------
// SPI1 (external flash) — unified with the custom backend (dualspi base).
//
// SPI1 is a ChibiOS driver instance (SPID1): flash reads + provisioning go through
// spiExchange(&SPID1); the DMA blit passes &SPID1 (hardware-confirmed: the SPI0
// restore-gate was the real fix, not SPI1 handling). Only the DMA command phase
// pokes SN_SPI1 directly (spi1_raw_byte), with the vector disabled by the extension.
// NOTE: this flash layer is duplicated from the custom lcd_bus.c; a later polish
// factors it into a shared flash_io.c.
// ---------------------------------------------------------------------------
static const SPIConfig flashcfg = {
    .ctrl0  = SPI_DATA_LENGTH(8),
    .ctrl1  = SPI_MLSB_MSB | SPI_CPOL_LOW | SPI_CPHA_FALLING,   // mode 0, MSB first
    .clkdiv = 0,                                                // 24 MHz
};
static bool spi1_inited = false;
static void spi1_setup(void) {
    SN_SYS1->AHBCLKEN |= (1u << 15) | (1u << 26);    // EBI+LCD (shared DMA datapath)
    SN_PFPA->SPI_b.MISO1 = 0b01; SN_PFPA->SPI_b.MOSI1 = 0b01;
    SN_PFPA->SPI_b.SCK1 = 0b11;  SN_PFPA->SPI_b.SEL1  = 0b01;
    gpio_set_pin_output(FLASH_CS); gpio_write_pin(FLASH_CS, 1);
    spiStart(&SPID1, &flashcfg);
    SN_SPI1->DFDLY_b.DFETCH_EN = 1;                  // DMA source auto-fetch
    SN_SPI1->CTRL0_b.FRESET = 0b11;
}
void lcd_flash_init(void) { if (!spi1_inited) { spi1_setup(); spi1_inited = true; } }
static bool spi1_xfer(uint8_t out) { spiSend(&SPID1, 1, &out); return true; }
static uint8_t spi1_rw(uint8_t out) { uint8_t in = 0xFF; spiExchange(&SPID1, 1, &out, &in); return in; }
// Bare-metal single byte on SPI1, used for the DMA READ+addr command phase.
static inline void spi1_raw_byte(uint8_t out) {
    SN_SPI1->DATA = out; uint32_t n = 0;
    while (SN_SPI1->STAT_b.BUSY) { if (++n > 500000u) break; }
    (void)SN_SPI1->DATA;
}

// ---- External flash provisioning (duplicated from custom lcd_bus.c) --------
#define FLASH_CMD_WREN      0x06
#define FLASH_CMD_RDSR      0x05
#define FLASH_CMD_PAGE_PROG 0x02
#define FLASH_CMD_SEC_ERASE 0x20
#define FLASH_CMD_JEDEC     0x9F
#define FLASH_PAGE          256u
#define FLASH_SECTOR        4096u
#define FLASH_CHIP_SIZE     0x1000000u
#define FLASH_ASSET_BASE    0x0CE0000u
static bool flash_unlocked = false;
void flash_set_unlocked(bool on) { flash_unlocked = on; }
static bool in_anim_slot(uint32_t a, uint32_t len) {
    static const uint32_t slots[] = {0x1AA000u, 0x200000u, 0x38B000u, 0x540000u};
    for (uint8_t i = 0; i < 4; i++)
        if (a >= slots[i] && a + len <= slots[i] + 0x100u + 132u * 0x8000u) return true;
    return false;
}
bool flash_writable(uint32_t addr, uint32_t len) {
    if (!len || addr >= FLASH_CHIP_SIZE || len > FLASH_CHIP_SIZE - addr) return false;
    if (addr >= FLASH_ASSET_BASE) return true;
    return flash_unlocked && in_anim_slot(addr, len);
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
    if (anim_active()) return false;
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
// ---- end duplicated flash provisioning -------------------------------------

// ---------------------------------------------------------------------------
// LCD command/window helpers (bare-metal, used only while we hold SPI0).
// ---------------------------------------------------------------------------
static inline void cs(bool hi) { gpio_write_pin(PANEL_CS, hi); }
static inline void dc(bool data){ gpio_write_pin(PANEL_DC, data); }
static void tx8(uint8_t b) {
    SN_SPI0->DATA = b;
    uint32_t n = 0; while (SN_SPI0->STAT_b.RX_EMPTY) { if (++n > 100000u) break; }
    (void)SN_SPI0->DATA;
}
static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    x0 += LCD_OFF_X; x1 += LCD_OFF_X; y0 += LCD_OFF_Y; y1 += LCD_OFF_Y;
    cs(0);
    dc(0); tx8(0x2A); dc(1); tx8(x0>>8); tx8(x0); tx8(x1>>8); tx8(x1);
    dc(0); tx8(0x2B); dc(1); tx8(y0>>8); tx8(y0); tx8(y1>>8); tx8(y1);
    dc(0); tx8(0x2C); dc(1);
}

// ---------------------------------------------------------------------------
// Flash->LCD DMA via the driver extension; completion calls blit_done_cb.
// ---------------------------------------------------------------------------
static volatile bool blit_done = true;

// Runs in the driver's SPI0 ISR at DMA transfer-complete (after the driver has
// drained the bus and restored 8-bit mode). We only drop the flash + panel CS.
static void blit_done_cb(void) {
    gpio_write_pin(FLASH_CS, 1);
    cs(1);
    blit_done = true;
}

static void blit_arm(uint32_t addr) {
    blit_done = false;
    // SPI0 side: DMA-ready config + counts (driver borrows SPI0). flash=NULL: SPI1
    // is bare-metal here (QP backend), so the extension skips SPI1-vector handling.
    spiSN32FlashDmaPrepare(&SPID0, &SPID1, FRAME_BYTES);
    SN_SPI1->CTRL0_b.FRESET = 0b11;                 // flash side (bare-metal, ours)
    // Command phase (SPI0 still 8-bit): panel window + flash READ+addr on SPI1.
    lcd_window(0, 0, FRAME_W - 1, FRAME_H - 1);
    gpio_write_pin(FLASH_CS, 0);
    spi1_raw_byte(FLASH_CMD_READ); spi1_raw_byte((addr>>16)&0xFF); spi1_raw_byte((addr>>8)&0xFF); spi1_raw_byte(addr&0xFF);
    SN_SPI1->IC = 0x3F;
    // Flip to 16-bit pixels and arm; blit_done_cb fires at completion.
    spiSN32FlashDmaFire(&SPID0, blit_done_cb);
}

// ---------------------------------------------------------------------------
// Animation player
// ---------------------------------------------------------------------------
static bool    anim_on  = false;
static uint8_t anim_idx = 0;

// True while the animation owns the bus; callers suspend RTC I2C polling then.
bool anim_active(void) { return anim_on; }

void anim_toggle(void) {
    static bool busy = false;
    if (busy) return;                       // re-entrancy guard: drop a press while mid-toggle
    busy = true;
    if (!spi1_inited) { spi1_setup(); spi1_inited = true; }
    if (!anim_on) {
        display_set_paused(true);           // QP stops touching SPI0
        anim_on = true; anim_idx = 0;
        blit_arm(ANIM_BASE + ANIM_HDR);     // MADCTL already 0xA8 (== QP rotation 270)
    } else {
        anim_on = false;
        uint32_t t = timer_read32();
        while (!blit_done && timer_elapsed32(t) < 100) { /* finish in-flight frame; bounded */ }
        SN_SPI0->DMACTRL_b.DMAEN = 0;       // stop a (possibly stalled) DMA
        SN_SPI0->CTRL0_b.FRESET = 0b11;     // clean SPI0/flash so QP's next spiStart is sane
        SN_SPI1->CTRL0_b.FRESET = 0b11;     // (ChibiOS re-configures SPI0 fully on the next spiStart)
        gpio_write_pin(FLASH_CS, 1); cs(1);
        blit_done = true;
        display_set_paused(false);          // resume + full repaint
    }
    busy = false;
}
void anim_task(void) {
    if (!anim_on || !blit_done) return;
    anim_idx = (anim_idx + 1) % ANIM_COUNT;
    blit_arm(ANIM_BASE + ANIM_HDR + (uint32_t)anim_idx * ANIM_STRIDE);
}
