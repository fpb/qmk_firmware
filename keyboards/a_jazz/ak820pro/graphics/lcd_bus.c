// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bare-metal LCD bus: we own SPI0 (+ Vector58) and SPI1 (flash). The GC9107 panel
// and the dashboard are driven entirely bare-metal (no Quantum Painter); the flash
// animation runs on the interrupt-driven SPI-to-SPI DMA. See docs/LCD_FLASH_LAYER.md.

#include <string.h>

#include "quantum.h"
#include "gpio.h"
#include "lcd_bus.h"

extern void display_set_paused(bool paused);   // graphics/display.c

// --- pins --------------------------------------------------------------------
#define PANEL_DC   D14
#define PANEL_CS   B8
#define PANEL_RST  A17
#define FLASH_CS   A13

#define FRAME_W 128
#define FRAME_H 128
#define FRAME_BYTES (FRAME_W * FRAME_H * 2)
#define LCD_OFF_X 1
#define LCD_OFF_Y 2
#define FLASH_CMD_READ 0x03

// GC9107 MADCTL. Rotation 270 = BGR(0x08) | MV(0x20) | MY(0x80) = 0xA8. The dashboard
// and the animation share this orientation.
#define MADCTL_270  0xA8
#define MADCTL_ANIM MADCTL_270

// Mario animation
#define ANIM_BASE   0x540000u
#define ANIM_HDR    0x100u
#define ANIM_STRIDE 0x8000u
#define ANIM_COUNT  8

#define NVIC_ISER ((volatile uint32_t *)0xE000E100)
#define NVIC_ICER ((volatile uint32_t *)0xE000E180)
#define NVIC_ICPR ((volatile uint32_t *)0xE000E280)
#define SPI0_IRQ  6

// ---------------------------------------------------------------------------
// Low-level bus
// ---------------------------------------------------------------------------
static void spi0_setup(void) {
    SN_SYS1->AHBCLKEN |= (1u << 12);
    SN_SPI0->CTRL0_b.SPIEN  = 0;
    SN_SPI0->CTRL0_b.MS     = 0;
    SN_SPI0->CTRL0_b.SDODIS = 0;
    SN_SPI0->CTRL0_b.DL     = 7;       // 8-bit
    SN_SPI0->CTRL0_b.SELDIS = 1;
    SN_SPI0->CTRL1_b.MLSB   = 0;
    SN_SPI0->CTRL1_b.CPOL   = 0;
    SN_SPI0->CTRL1_b.CPHA   = 0;
    SN_SPI0->CLKDIV_b.DIV   = 0;       // 24MHz (CPU writes corrupt at <=12MHz)
    SN_SPI0->DFDLY_b.DFETCH_EN = 1;
    SN_SPI0->CTRL0_b.FRESET = 0b11;
    SN_SPI0->CTRL0_b.SPIEN  = 1;
}

static bool spi1_inited = false;
static void spi1_setup(void) {
    SN_SYS1->AHBCLKEN |= (1u << 13);
    SN_SYS1->AHBCLKEN |= (1u << 15) | (1u << 26);   // EBI+LCD (shared DMA datapath)
    // NOTE: do NOT touch SN_FLASH->LPCTRL here. ChibiOS sets it to 0x5AFA0029 (correct
    // wait-states for 48MHz). Overriding it to the ">48MHz" preset (0x39) added extra
    // internal-flash wait-states that slowed CPU instruction fetch (~5% matrix-scan
    // drop that persisted after the first animation). It was never needed for the DMA.
    SN_PFPA->SPI_b.MISO1 = 0b01; SN_PFPA->SPI_b.MOSI1 = 0b01;
    SN_PFPA->SPI_b.SCK1 = 0b11;  SN_PFPA->SPI_b.SEL1  = 0b01;
    gpio_set_pin_output(FLASH_CS); gpio_write_pin(FLASH_CS, 1);
    SN_SPI1->CTRL0_b.SPIEN=0; SN_SPI1->CTRL0_b.MS=0; SN_SPI1->CTRL0_b.SDODIS=0;
    SN_SPI1->CTRL0_b.DL=7; SN_SPI1->CTRL0_b.SELDIS=1;
    SN_SPI1->CTRL1_b.MLSB=0; SN_SPI1->CTRL1_b.CPOL=0; SN_SPI1->CTRL1_b.CPHA=0;
    SN_SPI1->CLKDIV_b.DIV=0; SN_SPI1->DFDLY_b.DFETCH_EN=1; SN_SPI1->IE_b.RXFIFOTHIE=1;
    SN_SPI1->CTRL0_b.FRESET=0b11; SN_SPI1->CTRL0_b.SPIEN=1;
}

static inline void cs(bool hi) { gpio_write_pin(PANEL_CS, hi); }
static inline void dc(bool data){ gpio_write_pin(PANEL_DC, data); }

static void tx8(uint8_t b) {
    SN_SPI0->DATA = b;
    uint32_t n = 0; while (SN_SPI0->STAT_b.RX_EMPTY) { if (++n > 100000u) break; }
    (void)SN_SPI0->DATA;
}

// --- pipelined bulk writer -------------------------------------------------
// tx8() pays a full RX round-trip per byte (write, wait for the echo, discard), so a
// screenful costs far more than the wire time. For pixel payloads we instead keep the
// TX FIFO full and drain RX opportunistically -- bytes then stream back-to-back at the
// SPI clock. Caller must have set the window already (CS low, DC=data).
static inline void rx_drain(void) { while (!SN_SPI0->STAT_b.RX_EMPTY) (void)SN_SPI0->DATA; }

static inline void tx_pipe(uint8_t b) {
    uint32_t g = 0;
    while (SN_SPI0->STAT_b.TX_FULL) {          // only stall when the FIFO is actually full
        rx_drain();
        if (++g > 1000000u) break;
    }
    SN_SPI0->DATA = b;
    if (!SN_SPI0->STAT_b.RX_EMPTY) (void)SN_SPI0->DATA;   // keep RX from backing up
}

// Wait for the last queued byte to leave the shifter, then discard its echo.
static void tx_flush(void) {
    uint32_t g = 0;
    while ((!SN_SPI0->STAT_b.TX_EMPTY || SN_SPI0->STAT_b.BUSY) && ++g < 1000000u) rx_drain();
    rx_drain();
}

// RGB565 is streamed hi-byte-first to match the panel.
static void tx_pixels(const uint16_t *px, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        tx_pipe((uint8_t)(px[i] >> 8));
        tx_pipe((uint8_t)(px[i] & 0xFF));
    }
    tx_flush();
}

static void reset_panel(void) {
    gpio_set_pin_output(PANEL_RST);
    gpio_write_pin(PANEL_RST, 1); wait_ms(20);
    gpio_write_pin(PANEL_RST, 0); wait_ms(20);
    gpio_write_pin(PANEL_RST, 1); wait_ms(200);
}

// Address window + RAMWR (leaves CS asserted, DC=data). Used by the DMA blit.
static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    x0 += LCD_OFF_X; x1 += LCD_OFF_X; y0 += LCD_OFF_Y; y1 += LCD_OFF_Y;
    cs(0);
    dc(0); tx8(0x2A); dc(1); tx8(x0>>8); tx8(x0); tx8(x1>>8); tx8(x1);
    dc(0); tx8(0x2B); dc(1); tx8(y0>>8); tx8(y0); tx8(y1>>8); tx8(y1);
    dc(0); tx8(0x2C); dc(1);
}

// ---------------------------------------------------------------------------
// Bare-metal dashboard drawing (replaces Quantum Painter). RGB565 is streamed
// hi-byte-first to match the panel.
//
// Byte order differs by path, and both are correct -- verified on hardware by
// drawing the same stock asset (usb_dongle, flash 0x0D8310) each way and getting
// an identical green dongle:
//   RAM  -> CPU  (here):            uint16 colour values, emitted hi byte first.
//   flash-> DMA  (lcd_blit_flash):  bytes stored LO first; CTRL0.DL=0xF packs the
//                                   pair into a 16-bit word and shifts it out MSB
//                                   first, which swaps them back.
// So a RAM tile promoted to flash must have its bytes SWAPPED on the way in --
// it is not a straight copy. See docs/LCD_FLASH_LAYER.md (Stage D).
// ---------------------------------------------------------------------------
void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    if (x1 < x0 || y1 < y0) return;
    lcd_window(x0, y0, x1, y1);
    uint32_t px = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (uint32_t i = 0; i < px; i++) { tx_pipe(hi); tx_pipe(lo); }
    tx_flush();
    cs(1);
}

// Stage C: blit a w*h RGB565 tile from RAM (firmware array) to (x,y).
// The SN32 DMA is SPI-to-SPI only -- its source is the other SPI's RX FIFO, with no
// source-address register -- so RAM-resident art cannot be DMA'd and is CPU-pushed here
// (pipelined, ~wire speed). Only flash-resident art can use lcd_blit_flash(). See
// docs/LCD_FLASH_LAYER.md.
void lcd_blit_ram(const uint16_t *px, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!px || !w || !h) return;
    lcd_window(x, y, x + w - 1, y + h - 1);
    tx_pixels(px, (uint32_t)w * (uint32_t)h);
    cs(1);
}

// ---------------------------------------------------------------------------
// Panel bring-up (bare-metal GC9107 init; literal opcodes, no Quantum Painter)
// ---------------------------------------------------------------------------
static void send_cmd(uint8_t c) { dc(0); tx8(c); dc(1); }
static void send_seq(const uint8_t *seq, uint32_t len) {   // cmd, delay_ms, nparams, params...
    cs(0);
    for (uint32_t i = 0; i < len;) {
        uint8_t cmd = seq[i], delay = seq[i+1], num = seq[i+2];
        send_cmd(cmd);
        for (uint8_t k = 0; k < num; k++) tx8(seq[i+3+k]);
        if (delay) wait_ms(delay);
        i += 3 + num;
    }
    cs(1);
}

void lcd_init(void) {
    gpio_set_pin_output(PANEL_CS); gpio_write_pin(PANEL_CS, 1);
    gpio_set_pin_output(PANEL_DC); gpio_write_pin(PANEL_DC, 1);
    spi0_setup();
    reset_panel();
    static const uint8_t seq[] = {
        0xFE, 5, 0,                 // inter-register enable 1
        0xEF, 5, 0,                 // inter-register enable 2
        0xB6, 0, 1, 0x19,           // function ctl6: allow complement-RGB + framerate
        0xAC, 0, 1, 0xC0,           // complement RGB
        0xAB, 0, 1, 0x0E,
        0xA8, 0, 1, 0x19,           // frame rate
        0x3A, 0, 1, 0x05,           // pixel format: 16bpp RGB565
        0x11, 120, 0,               // sleep out
        0x29, 20, 0,                // display on
        0x36, 0, 1, MADCTL_270,     // memory access ctl: rotation 270
    };
    send_seq(seq, sizeof(seq));
}

// ---------------------------------------------------------------------------
// SPI-to-SPI DMA (interrupt-driven via Vector58)
// ---------------------------------------------------------------------------
static bool spi1_xfer(uint8_t out) {
    SN_SPI1->DATA = out; uint32_t n = 0;
    while (SN_SPI1->STAT_b.BUSY) { if (++n > 500000u) return false; }
    (void)SN_SPI1->DATA; return true;
}
__attribute__((unused)) static uint8_t spi1_rw(uint8_t out) {
    SN_SPI1->DATA = out; uint32_t n = 0;
    while (SN_SPI1->STAT_b.RX_EMPTY) { if (++n > 2000u) break; }
    return (uint8_t)SN_SPI1->DATA;
}
// ---------------------------------------------------------------------------
// External flash WRITE path (Stage D provisioning)
//
// Everything here is non-blocking with respect to the *device*: a command is
// issued over SPI (microseconds) and the chip then goes busy on its own -- a
// page program takes ~1-3 ms, a 4K sector erase 50-300 ms. Blocking the matrix
// scan for 300 ms is not acceptable, so callers must poll flash_busy() instead
// of waiting here. Every SPI-level spin below is bounded; an unbounded one
// hangs the keyboard before USB enumerates.
// ---------------------------------------------------------------------------
#define FLASH_CMD_WREN      0x06
#define FLASH_CMD_RDSR      0x05
#define FLASH_CMD_PAGE_PROG 0x02
#define FLASH_CMD_SEC_ERASE 0x20
#define FLASH_CMD_JEDEC     0x9F
#define FLASH_PAGE          256u
#define FLASH_SECTOR        4096u
#define FLASH_CHIP_SIZE     0x1000000u   // PY25Q128HA, 16MB

// Write policy. The stock LCD assets below 0x1AA000 are effectively
// irreplaceable (our only dump of them has read damage), so they are never
// writable. The animation slots are stock-owned but legitimately rewritable,
// behind an explicit unlock. Our own Stage D assets live in the 3.12 MB that
// has been erased (0xFF) since manufacture and is always writable.
#define FLASH_ASSET_BASE    0x0CE0000u
static bool flash_unlocked = false;

void flash_set_unlocked(bool on) { flash_unlocked = on; }

// Animation slots seen in stock firmware: V1.13 boot/user, V1.14 boot/user.
static bool in_anim_slot(uint32_t a, uint32_t len) {
    static const uint32_t slots[] = {0x1AA000u, 0x200000u, 0x38B000u, 0x540000u};
    for (uint8_t i = 0; i < 4; i++) {
        // Slots are sized generously: header + up to 132 frames.
        if (a >= slots[i] && a + len <= slots[i] + 0x100u + 132u * 0x8000u) return true;
    }
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

// True while a program/erase is still running (status bit 0 = WIP).
bool flash_busy(void) {
    lcd_flash_init();
    return (flash_status() & 0x01) != 0;
}

static void flash_wren(void) {
    gpio_write_pin(FLASH_CS, 0);
    spi1_xfer(FLASH_CMD_WREN);
    gpio_write_pin(FLASH_CS, 1);
}

uint32_t flash_jedec_id(void) {
    lcd_flash_init();
    gpio_write_pin(FLASH_CS, 0);
    spi1_xfer(FLASH_CMD_JEDEC);
    uint32_t id = ((uint32_t)spi1_rw(0xFF) << 16);
    id |= ((uint32_t)spi1_rw(0xFF) << 8);
    id |= spi1_rw(0xFF);
    gpio_write_pin(FLASH_CS, 1);
    return id;
}

void flash_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len) {
    lcd_flash_init();
    flash_cmd_addr(FLASH_CMD_READ, addr);
    for (uint32_t i = 0; i < len; i++) dst[i] = spi1_rw(0xFF);
    gpio_write_pin(FLASH_CS, 1);
}

// Erase one 4K sector. Returns false if the address is not writable, the chip
// is still busy, or the animation owns the bus. The chip stays busy for
// 50-300 ms afterwards -- poll flash_busy().
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

// Program up to one 256-byte page. The write must not cross a page boundary --
// the chip wraps to the start of the page instead of continuing, silently
// corrupting data, so that case is rejected rather than split here.
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

// CRC32 (IEEE, reflected) over a flash range, so the host can verify a large
// upload without reading megabytes back over 29-byte HID packets.
uint32_t flash_crc32(uint32_t addr, uint32_t len) {
    lcd_flash_init();
    uint32_t crc = 0xFFFFFFFFu;
    flash_cmd_addr(FLASH_CMD_READ, addr);
    for (uint32_t i = 0; i < len; i++) {
        crc ^= spi1_rw(0xFF);
        for (uint8_t b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
    gpio_write_pin(FLASH_CS, 1);
    return ~crc;
}

static volatile bool blit_done = true;

void Vector58(void) {
    uint32_t ris = SN_SPI0->RIS;
    SN_SPI0->IC = 0x3F;
    if (ris & (1u << 5)) {                  // DMATCIF
        for (uint32_t g = 0; g < 200000u && (!SN_SPI0->STAT_b.TX_EMPTY || SN_SPI0->STAT_b.BUSY); g++) { }
        SN_SPI0->DMACTRL_b.DMAEN = 0;
        SN_SPI0->CTRL0_b.DL = 7;
        gpio_write_pin(FLASH_CS, 1);
        cs(1);
        blit_done = true;
    }
}

// Stage C: blit a w*h RGB565 tile from flash offset `src` to the panel rect at (x,y).
// Interrupt-driven and NON-BLOCKING: arms the SPI1(flash)->SPI0(LCD) engine and returns;
// Vector58 signals completion via blit_done. Animation frames are just the full-frame case.
// NOTE: the panel's MADCTL orientation is the caller's business -- flash art authored for
// the animation orientation (MADCTL_ANIM) will not match the dashboard's (MADCTL_270).
void lcd_blit_flash(uint32_t src, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!w || !h) return;
    uint32_t bytes = (uint32_t)w * (uint32_t)h * 2u;
    blit_done = false;
    // Fully re-init SPI0 so the DMA sees a pristine peripheral (SPIEN=0 + FRESET re-latches
    // DFETCH_EN, which a live FRESET can't) after any CPU-write drawing.
    spi0_setup();
    SN_SPI0->CTRL0_b.FRESET = 0b11; SN_SPI1->CTRL0_b.FRESET = 0b11;
    SN_SPI0->DMACTRL_b.DMAEN = 0; SN_SPI0->DMACTRL_b.DIR = 0;
    SN_SPI0->DMACNT_b.CNT = bytes - 1; SN_SPI0->DMAHTCNT_b.HTCNT = (bytes - 1) / 2;
    lcd_window(x, y, x + w - 1, y + h - 1);
    gpio_write_pin(FLASH_CS, 0);
    spi1_xfer(FLASH_CMD_READ); spi1_xfer((src>>16)&0xFF); spi1_xfer((src>>8)&0xFF); spi1_xfer(src&0xFF);
    SN_SPI0->IC = 0x3F; SN_SPI1->IC = 0x3F;
    SN_SPI0->CTRL0_b.DL = 0xF;               // 16-bit TX (one RGB565 pixel per word)
    SN_SPI0->IE = (1u << 5) | (1u << 4);
    NVIC_ICPR[0] = (1u << SPI0_IRQ);
    NVIC_ISER[0] = (1u << SPI0_IRQ);        // we own Vector58 -> real interrupt-driven completion
    SN_SPI0->DMACTRL_b.DMAEN = 1;   // interrupt-driven; Vector58 signals completion (non-blocking)
}

// Animation frames are full-screen tiles.
static inline void blit_arm(uint32_t addr) { lcd_blit_flash(addr, 0, 0, FRAME_W, FRAME_H); }

// True once the in-flight DMA blit has completed (Vector58 sets it).
bool lcd_blit_busy(void) { return !blit_done; }

// --- QFF text (grayscale, RLE) ---------------------------------------------
// Locate a character's tile. Glyphs are stored contiguously in charset order, so the
// charset index IS the tile index -- no per-glyph table needed.
static const uint16_t *glyph_tile(const lcd_font_t *f, char c) {
    const char *p = strchr(f->charset, c);
    if (!p || !c) return NULL;                       // strchr matches the NUL terminator
    return f->px + (uint32_t)(p - f->charset) * f->cell_w * f->cell_h;
}

// Monospace: every cell has the same advance, so width is just the character count.
// Unknown characters still advance (they blit nothing), keeping layout stable.
uint16_t lcd_text_width(const lcd_font_t *f, const char *s) {
    return (uint16_t)(strlen(s) * f->cell_w);
}

void lcd_draw_text(uint16_t x, uint16_t y, const lcd_font_t *f, const char *s) {
    for (; *s; s++, x += f->cell_w) {
        const uint16_t *g = glyph_tile(f, *s);
        if (g) lcd_blit_ram(g, x, y, f->cell_w, f->cell_h);
    }
}

void lcd_draw_image(const lcd_image_t *img, uint16_t x, uint16_t y) {
    lcd_blit_ram(img->px, x, y, img->w, img->h);
}

// ---------------------------------------------------------------------------
// Animation player
// ---------------------------------------------------------------------------
static bool    anim_on  = false;
static uint8_t anim_idx = 0;

// True while the flash-animation player owns the bus. The bit-banged RTC I2C (SCL=A14,
// SDA=A15) shares port A with the flash SPI1 pins (SCK=A12, CS=A13); its open-drain
// pin-mode toggling glitches A12/A13 mid-DMA and corrupts the flash read. Callers must
// suspend RTC polling while this is true.
bool anim_active(void) { return anim_on; }

static void set_madctl(uint8_t v) { cs(0); dc(0); tx8(0x36); dc(1); tx8(v); cs(1); }

// SPI1 (external flash) is brought up lazily -- lcd_blit_flash does NOT do it,
// so any caller outside the animation path must call this first.
void lcd_flash_init(void) {
    if (!spi1_inited) { spi1_setup(); spi1_inited = true; }
}

// One-shot self-contained flash blit: brings up SPI1, blits, waits with a bound,
// then puts SPI0 back exactly as anim_toggle's stop path does so the dashboard
// runs unaffected. The wait is bounded on purpose -- completion rides the SPI0
// IRQ, and an unbounded spin here hangs the keyboard before USB enumerates.
void lcd_blit_flash_probe(uint32_t src, uint16_t w, uint16_t h) {
    lcd_flash_init();
    lcd_blit_flash(src, 0, 0, w, h);
    for (uint32_t i = 0; i < 4000000u && !blit_done; i++) { __asm__ volatile("nop"); }
    gpio_write_pin(FLASH_CS, 1); cs(1);
    NVIC_ICER[0] = (1u << SPI0_IRQ);
    SN_SPI0->IE  = 0;
    spi0_setup();
}

void anim_toggle(void) {
    lcd_flash_init();
    if (!anim_on) {
        display_set_paused(true);           // stop QP touching the bus
        set_madctl(MADCTL_ANIM);            // frames authored for this orientation
        anim_on = true; anim_idx = 0;
        blit_arm(ANIM_BASE + ANIM_HDR);
    } else {
        anim_on = false;
        while (!blit_done) { /* let the in-flight frame finish */ }
        gpio_write_pin(FLASH_CS, 1); cs(1);
        // Return SPI0 to its exact boot state so the dashboard runs no heavier than at
        // start: kill the DMA-completion IRQ and re-init the SPI (clears leftover DMA
        // config / interrupt-enable that otherwise adds per-pass overhead).
        NVIC_ICER[0] = (1u << SPI0_IRQ);
        SN_SPI0->IE  = 0;
        spi0_setup();
        set_madctl(MADCTL_270);             // restore dashboard orientation
        display_set_paused(false);          // resume + full repaint
    }
}
void anim_task(void) {
    if (!anim_on || !blit_done) return;     // blit_arm blocks until done, so this just paces frames
    anim_idx = (anim_idx + 1) % ANIM_COUNT;
    blit_arm(ANIM_BASE + ANIM_HDR + (uint32_t)anim_idx * ANIM_STRIDE);
}
