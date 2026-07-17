// Copyright 2026 Fernando Birra
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bare-metal LCD bus: we own SPI0 (+ Vector58) and SPI1 (flash). The GC9107 panel
// and the dashboard are driven entirely bare-metal (no Quantum Painter); the flash
// animation runs on the interrupt-driven SPI-to-SPI DMA. See docs/LCD_FLASH_LAYER.md.

#include "quantum.h"
#include "gpio.h"
#include "color.h"      // hsv_to_rgb_nocie for paletted QGF images
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
// hi-byte-first to match the panel (proven by the flash animation path).
// ---------------------------------------------------------------------------
void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    if (x1 < x0 || y1 < y0) return;
    lcd_window(x0, y0, x1, y1);
    uint32_t px = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (uint32_t i = 0; i < px; i++) { tx8(hi); tx8(lo); }
    cs(1);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd24(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16); }

// A pull-based byte source over an (optionally RLE) stream, shared by the image and
// font decoders. Byte-level RLE: marker M>=128 -> literal run of (M-127) bytes; M<128
// -> repeat the next byte M times.
typedef struct { const uint8_t *p, *end; bool rle; uint8_t mode; uint16_t remain; uint8_t val; } bytesrc_t;
static uint8_t bs_next(bytesrc_t *b) {
    if (!b->rle) return (b->p < b->end) ? *b->p++ : 0;
    if (b->remain == 0) {
        if (b->p >= b->end) return 0;
        uint8_t m = *b->p++;
        if (m >= 128) { b->mode = 1; b->remain = m - 127; }                        // literal run
        else          { b->mode = 0; b->remain = m; b->val = (b->p < b->end) ? *b->p++ : 0; } // repeat
    }
    b->remain--;
    return (b->mode == 1) ? ((b->p < b->end) ? *b->p++ : 0) : b->val;
}

// Draw a QP QGF image at (x,y): native RGB565 (format 0x08) or paletted (0x04..0x07,
// e.g. the 8bpp splash) with an HSV palette converted to RGB565.
void lcd_draw_qgf(uint16_t x, uint16_t y, const uint8_t *qgf) {
    uint16_t w = rd16(qgf + 17), h = rd16(qgf + 19);
    uint32_t foff = rd24(qgf + 28);                 // frame_offsets block payload = offset[0]
    uint8_t  fmt  = qgf[foff + 5];                  // frame descriptor: format @ +5
    uint8_t  comp = qgf[foff + 7];                  //                   compression @ +7
    const uint8_t *blk = qgf + foff + 11;           // first block after the frame descriptor

    static uint16_t pal[256];                        // static: keep it off the M0 stack
    uint8_t bpp = 0;
    if (fmt >= 0x04 && fmt <= 0x07) {                // paletted
        bpp = 1u << (fmt - 4);                        // 0x04->1bpp .. 0x07->8bpp
        if (blk[0] == 0x03) {                         // palette block: HSV888 entries
            uint32_t plen = rd24(blk + 2), nent = plen / 3;
            const uint8_t *pe = blk + 5;
            for (uint32_t i = 0; i < nent && i < 256; i++) {
                hsv_t hsv = { pe[i*3], pe[i*3+1], pe[i*3+2] };
                rgb_t rgb = hsv_to_rgb_nocie(hsv);
                pal[i] = (uint16_t)(((rgb.r >> 3) << 11) | ((rgb.g >> 2) << 5) | (rgb.b >> 3));
            }
            blk += 5 + plen;
        }
    } else if (fmt != 0x08) {
        return;                                       // unsupported format
    }
    while (blk[0] != 0x05) blk += 5 + rd24(blk + 2);  // seek the data block
    bytesrc_t bs = { .p = blk + 5, .end = blk + 5 + rd24(blk + 2), .rle = (comp != 0) };
    uint32_t npix = (uint32_t)w * h;

    lcd_window(x, y, x + w - 1, y + h - 1);
    if (fmt == 0x08) {                                // native RGB565: 2 bytes/pixel, panel order
        for (uint32_t i = 0; i < npix * 2; i++) tx8(bs_next(&bs));
    } else {                                          // paletted: bpp-bit indices (LSB first)
        uint8_t mask = (1u << bpp) - 1, cur = 0, bits = 0;
        for (uint32_t i = 0; i < npix; i++) {
            if (bits == 0) { cur = bs_next(&bs); bits = 8; }
            uint16_t col = pal[cur & mask]; cur >>= bpp; bits -= bpp;
            tx8(col >> 8); tx8(col & 0xFF);
        }
    }
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

static void blit_arm(uint32_t addr) {
    blit_done = false;
    // QP has been driving SPI0 (CPU writes) -- fully re-init it so the DMA sees the
    // exact pristine SPI0 the standalone had (SPIEN=0 + FRESET re-latches DFETCH_EN,
    // which a live FRESET can't). Then match the standalone's blit order exactly.
    spi0_setup();
    SN_SPI0->CTRL0_b.FRESET = 0b11; SN_SPI1->CTRL0_b.FRESET = 0b11;
    SN_SPI0->DMACTRL_b.DMAEN = 0; SN_SPI0->DMACTRL_b.DIR = 0;
    SN_SPI0->DMACNT_b.CNT = FRAME_BYTES - 1; SN_SPI0->DMAHTCNT_b.HTCNT = (FRAME_BYTES - 1) / 2;
    lcd_window(0, 0, FRAME_W - 1, FRAME_H - 1);
    gpio_write_pin(FLASH_CS, 0);
    spi1_xfer(FLASH_CMD_READ); spi1_xfer((addr>>16)&0xFF); spi1_xfer((addr>>8)&0xFF); spi1_xfer(addr&0xFF);
    SN_SPI0->IC = 0x3F; SN_SPI1->IC = 0x3F;
    SN_SPI0->CTRL0_b.DL = 0xF;               // 16-bit TX (one RGB565 pixel per word)
    SN_SPI0->IE = (1u << 5) | (1u << 4);
    NVIC_ICPR[0] = (1u << SPI0_IRQ);
    NVIC_ISER[0] = (1u << SPI0_IRQ);        // we own Vector58 -> real interrupt-driven completion
    SN_SPI0->DMACTRL_b.DMAEN = 1;   // interrupt-driven; Vector58 signals completion (non-blocking)
}

// --- QFF text (grayscale, RLE) ---------------------------------------------
static uint16_t lerp565(uint16_t bg, uint16_t fg, uint8_t idx, uint8_t maxidx) {
    uint8_t br=(bg>>11)&0x1F, bgc=(bg>>5)&0x3F, bb=bg&0x1F;
    uint8_t fr=(fg>>11)&0x1F, fgc=(fg>>5)&0x3F, fb=fg&0x1F;
    uint8_t r = br + (fr - br) * idx / maxidx, g = bgc + (fgc - bgc) * idx / maxidx, b = bb + (fb - bb) * idx / maxidx;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
// 25 (font descriptor) + 290 (ascii table) + 5 (glyph-data block header) = 320.
// Grayscale fonts have no palette/unicode blocks. Glyph offsets are relative to here.
#define QFF_GLYPHDATA 320u

static const uint8_t *qff_glyph(const uint8_t *font, char c, uint8_t *width) {
    if (c < 0x20 || c > 0x7E) { *width = 0; return NULL; }
    const uint8_t *e = font + 30 + (uint32_t)(c - 0x20) * 3;   // 25 desc + 5 ascii-block header
    uint32_t v = e[0] | (e[1] << 8) | ((uint32_t)e[2] << 16);
    *width = v & 0x3F;                                        // 6-bit width
    return font + QFF_GLYPHDATA + (v >> 6);                   // 18-bit offset into glyph data
}
uint16_t lcd_text_width(const uint8_t *font, const char *s) {
    uint16_t w = 0; uint8_t gw;
    for (; *s; s++) { qff_glyph(font, *s, &gw); w += gw; }
    return w;
}
void lcd_draw_text(uint16_t x, uint16_t y, const uint8_t *font, const char *s, uint16_t fg, uint16_t bg) {
    uint8_t lh = font[17], fmt = font[21], comp = font[23];
    uint8_t bpp = (fmt == 0x00) ? 1 : (fmt == 0x01) ? 2 : (fmt == 0x02) ? 4 : 8;
    uint8_t mask = (1u << bpp) - 1, maxidx = mask;
    const uint8_t *fend = font + (font[9] | (font[10]<<8) | ((uint32_t)font[11]<<16));
    for (; *s; s++) {
        uint8_t gw; const uint8_t *g = qff_glyph(font, *s, &gw);
        if (!g || gw == 0) continue;
        bytesrc_t bs = { .p = g, .end = fend, .rle = (comp != 0) };
        lcd_window(x, y, x + gw - 1, y + lh - 1);
        uint32_t npix = (uint32_t)gw * lh; uint8_t cur = 0, bits = 0;
        for (uint32_t i = 0; i < npix; i++) {
            if (bits == 0) { cur = bs_next(&bs); bits = 8; }
            uint16_t col = lerp565(bg, fg, cur & mask, maxidx); cur >>= bpp; bits -= bpp;
            tx8(col >> 8); tx8(col & 0xFF);
        }
        cs(1);
        x += gw;
    }
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

void anim_toggle(void) {
    if (!spi1_inited) { spi1_setup(); spi1_inited = true; }
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
