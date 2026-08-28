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
// SPI1 (external flash) — QP backend: bare-metal, DMA-source only.
//
// The dualspi base makes SPI1 a ChibiOS driver instance (SPID1) so the custom
// backend can do CPU-path flash I/O through spiExchange(&SPID1). The QP backend
// has NO such I/O -- it embeds its dashboard/splash assets, so flash is only the
// animation DMA source. So we DON'T spiStart SPID1 here (its handler would never
// have a valid transfer to complete; re-enabling its vector after a DMA would
// resume an uninitialised thread ref and crash). SPI1 stays bare-metal and the
// blit passes flash=NULL to the extension, which then skips all SPI1-vector
// handling. RXFIFOTHIE stays set: on the SN32 it gates the RX-threshold event
// that triggers the DMA request.
// ---------------------------------------------------------------------------
static bool spi1_inited = false;
static void spi1_setup(void) {
    SN_SYS1->AHBCLKEN |= (1u << 13);                 // SPI1 clock (no spiStart to do it)
    SN_SYS1->AHBCLKEN |= (1u << 15) | (1u << 26);    // EBI+LCD (shared DMA datapath)
    SN_PFPA->SPI_b.MISO1 = 0b01; SN_PFPA->SPI_b.MOSI1 = 0b01;
    SN_PFPA->SPI_b.SCK1 = 0b11;  SN_PFPA->SPI_b.SEL1  = 0b01;
    gpio_set_pin_output(FLASH_CS); gpio_write_pin(FLASH_CS, 1);
    SN_SPI1->CTRL0_b.SPIEN=0; SN_SPI1->CTRL0_b.MS=0; SN_SPI1->CTRL0_b.SDODIS=0;
    SN_SPI1->CTRL0_b.DL=7; SN_SPI1->CTRL0_b.SELDIS=1;
    SN_SPI1->CTRL1_b.MLSB=0; SN_SPI1->CTRL1_b.CPOL=0; SN_SPI1->CTRL1_b.CPHA=0;
    SN_SPI1->CLKDIV_b.DIV=0; SN_SPI1->DFDLY_b.DFETCH_EN=1; SN_SPI1->IE_b.RXFIFOTHIE=1;
    SN_SPI1->CTRL0_b.FRESET=0b11; SN_SPI1->CTRL0_b.SPIEN=1;
}
// Bare-metal single byte on SPI1, used for the DMA READ+addr command phase.
static inline void spi1_raw_byte(uint8_t out) {
    SN_SPI1->DATA = out; uint32_t n = 0;
    while (SN_SPI1->STAT_b.BUSY) { if (++n > 500000u) break; }
    (void)SN_SPI1->DATA;
}

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
    spiSN32FlashDmaPrepare(&SPID0, NULL, FRAME_BYTES);
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
