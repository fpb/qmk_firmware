# AK820 Pro — flash-backed LCD layer (design + roadmap)

> **Status (2026-07): the foundation is DONE.** The bare-metal LCD bus, the interrupt-driven
> flash→LCD DMA animation, and QP/DMA coexistence are all working and shipped across three
> branches — see `docs/LCD_DMA_BRANCHES.md` for the branch-by-branch comparison. This file is
> now the **forward** roadmap (what's left) plus the design rationale, corrected for what we
> actually learned. Superseded predictions are called out inline.

## Vision (refined)
Drive as much of the LCD as possible from pre-rendered **RGB565 tiles** blitted with minimal
CPU in the data path — animation frames, images, and eventually fixed-size **glyph tiles** —
and retire Quantum Painter + its qgf/qff formats once nothing needs them.

**Hard hardware constraint (learned):** the SN32 "DMA" is the EBI/LCD **SPI-to-SPI** engine
(`DMACTRL.DIR` selects `SPI1(flash)→SPI0(LCD)` or the reverse). Its source is always the *other
SPI's RX FIFO* — there is **no source-address register, so it cannot stream from SRAM**, and
these SN32F2 parts have no general memory→peripheral DMA. Consequence:

- **Flash-resident content (animations)** → `flash → LCD` via **DMA**, non-blocking. ✅ shipped.
- **RAM-resident content (static images, glyphs)** → **CPU-pushed** (pipelined `tx_bulk`), *not*
  DMA. Fast enough (a full 128×128 push ≈ 11 ms once; partial digit updates are sub-ms).

This is the current design decision: **keep static assets in MCU firmware as RAM arrays and
CPU-draw them; keep only animations in external flash + DMA.** It deliberately avoids building a
flash-provisioning toolchain (flash-write routines + host uploader + asset map) — the expensive
part — since nothing new has to be written to external flash.

## What's already true (foundation, done)
- **Bare-metal LCD bus** (`graphics/lcd_bus.c`): SPI0 bring-up, SPI1 (flash), `lcd_window`,
  pipelined `tx_bulk`, and the SPI1→SPI0 flash→LCD DMA with completion via the SPI0 IRQ.
- **Animation** plays non-blocking from external flash (`ANIM_BASE`, 8×0x8000 frames at
  `hdr+0x100`; see [[ak820pro-flash-animation-layout]]).
- **QP/DMA coexistence — SOLVED**, three ways (details in `docs/LCD_DMA_BRANCHES.md`):
  - `ak820pro-flashlcd` — QP removed; bare-metal dashboard decodes qgf/qff in-house.
  - `ak820pro-flashlcd-qp` — QP over our custom comms; gate `qc_start/stop` on `anim_active()`.
  - `ak820pro-flashlcd-qp-lld` — QP over the **stock** gc9107 driver; DMA serviced via a weak
    hook on the driver's shared `Vector58`; `qp_internal_task` disabled via
    `QUANTUM_PAINTER_INTERNAL_TASK_DISABLE`.
- **Root cause of the old corruption (for the record):** `qp_internal_task()`→`qp_flush()`
  bracketed its no-op tft_panel flush with `qp_comms_start()/stop()`, toggling panel **CS (B8)**
  mid-DMA (fixed-in-time ~1 ms, not byte-count). All three fixes above neutralise that.

### Corrections to this doc's earlier predictions
- ~~"Interrupt-driven DMA requires us to own `Vector58` → `SN32_SPI_USE_SPI0=FALSE`."~~ Not the
  only way: the `-qp-lld` **tunnel** keeps `SN32_SPI_USE_SPI0=TRUE` and services DMA completion
  through a weak hook on the ChibiOS driver's `Vector58`. Owning the vector (bare-metal) is one
  option; tunnelling the stock driver's vector is another.
- ~~"The poll-based coexistence attempt is a dead end."~~ True for *polling*, but the broader
  "QP can't coexist with the DMA" implication was wrong — it was the CS toggle, now fixed.
- ~~"Move *all* LCD drawing onto the DMA."~~ Impossible on this silicon for RAM content (no
  RAM→SPI DMA); static assets are CPU-drawn. Only flash content is DMA-drawn.

## The two primitives everything is built on
```c
// DMA — flash-resident content (animation frames, and any flash tiles). Non-blocking:
// arms the SPI1->SPI0 engine, returns immediately, completion via IRQ/flag.
void lcd_blit_flash(uint32_t src, uint16_t x, uint16_t y, uint16_t w, uint16_t h);   // TODO: generalise

// CPU — RAM-resident content (static images, glyph tiles). Pipelined tx_bulk (FIFO kept full),
// blocking but fast; use partial rects to keep it sub-ms.
void lcd_blit_ram(const uint16_t *px, uint16_t x, uint16_t y, uint16_t w, uint16_t h);  // TODO
```
Today's animation uses the full-frame form of the flash blit; generalising to an arbitrary rect
is small (`DMACNT = w*h*2 - 1`, window `(x, y, x+w-1, y+h-1)`, arbitrary `src`). `lcd_blit_ram`
is a thin wrapper over `lcd_window` + `tx_bulk` from a RAM pointer.

## Remaining work (additive, no refactor)
- **`lcd_blit_ram(px,x,y,w,h)`** — the CPU tile blit for static content.
- **Migrate the dashboard off QP** onto `lcd_blit_ram` + `lcd_fill_rect`, incrementally, keeping
  the qgf/qff decoders as a fallback until each asset is a RAM tile. Then delete QP + decoders.
- **Fixed-size glyph atlas in RAM** — equal-size RGB565 tiles; `lcd_draw_char(c,x,y)` indexes the
  atlas → `lcd_blit_ram`; `lcd_draw_text` advances x by tile width. Colors are baked into the
  tiles (DMA/CPU-blit can't recolor/alpha-blend); a second color = a second atlas.
- **(Optional, deferred) flash asset index + provisioning** — only if you later decide some
  *large* static images should live in flash + DMA after all. That reintroduces the flash-write
  toolchain (WREN/erase/page-program on SPI1 + a raw-HID uploader + an id→addr,w,h table). Kept
  out of scope by the RAM-images decision above.

## Staged plan (updated)
- **Stage A — DONE.** Bare-metal `lcd_bus` (SPI0 + IRQ + SPI1), pipelined `tx_bulk`, fills,
  full-frame flash DMA, animation player.
- **Stage B — DONE.** Dashboard coexists with the DMA (three branches; see comparison doc).
- **Stage C — NEXT.** `lcd_blit_ram` + generalise `lcd_blit_flash` to a rect; move the dashboard
  onto RAM tiles + a fixed-size glyph atlas; drop QP and the qgf/qff decoders.
- **Stage D — deferred/optional.** Flash-resident static images + provisioning toolchain, only
  if RAM/firmware size ever forces it.

## Performance note (concrete, measured)
- Our **pipelined `tx_bulk`** streams at ~wire speed (24 MHz, ~3 MB/s) — a full frame ~13 ms.
- The **stock ChibiOS SN32 SPI** driver transfers **one byte per interrupt**, ~10× slower for
  bulk pixels — this is why `-qp-lld`'s full dashboard redraw dips the scan to ~700 Hz. It is
  driver overhead, **not** the SPI clock (divisor 2 already maps to clkdiv 0 = 24 MHz via a
  fall-through in `spi_master.c`'s SN32 divisor switch). See `docs/LCD_DMA_BRANCHES.md`.
