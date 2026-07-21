# AK820 Pro — flash-backed LCD layer (design + roadmap)

> **Status (2026-07): the foundation is DONE.** The bare-metal LCD bus, the interrupt-driven
> flash→LCD DMA animation, and QP/DMA coexistence are all working and shipped across three
> branches — see `docs/LCD_DMA_BRANCHES.md` for the branch-by-branch comparison. This file is
> now the **forward** roadmap (what's left) plus the design rationale, corrected for what we
> actually learned. Superseded predictions are called out inline.
>
> **Stage C is DONE** on `ak820pro-flashlcd-tiles`: the dashboard runs entirely on
> pre-rendered RGB565 tiles and the QGF/QFF decoders are gone.
>
> **Stage D is DONE too.** All art lives in external flash and is DMA-drawn; the
> firmware embeds none of it. Firmware dropped **133556 -> 75380 bytes (58 KB
> reclaimed**, 29% of the 256 KB budget). Provisioning runs over raw HID on VIA
> custom-value channel 0x11, driven by `ak820ctl`. Verified on hardware: splash,
> clock, date, icons and battery all render from flash with the matrix scan
> holding a steady 1396 Hz.

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

**Interim decision (Stage C, shipped):** static assets live in MCU firmware as RAM arrays and are
CPU-drawn; only animations are in external flash + DMA. This got the whole dashboard onto tiles
without needing a flash-provisioning toolchain.

**Endgame (Stage D):** move the assets into flash so they are DMA-drawn too. That is why *every*
asset is already RGB565 in `res/raw/` — the format the DMA and panel require — so the move is a
relocation, not a reformat. What it still costs is the provisioning toolchain: a flash **write**
path on SPI1 plus a host uploader. RAM-drawn static content stays available for anything small or
frequently recoloured.

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
void lcd_blit_flash(uint32_t src, uint16_t x, uint16_t y, uint16_t w, uint16_t h);   // DONE

// CPU — RAM-resident content (static images, glyph tiles). Pipelined tx_bulk (FIFO kept full),
// blocking but fast; use partial rects to keep it sub-ms.
void lcd_blit_ram(const uint16_t *px, uint16_t x, uint16_t y, uint16_t w, uint16_t h);  // DONE
```
Both exist as of Stage C. The flash blit takes an arbitrary rect (`DMACNT = w*h*2 - 1`, window
`(x, y, x+w-1, y+h-1)`, arbitrary `src`); the animation is simply its full-frame case.
`lcd_blit_ram` is a thin wrapper over `lcd_window` + the pipelined bulk writer.

## The asset pipeline (branch `ak820pro-flashlcd-tiles`)
`PNG -> raw RGB565 -> tile -> panel`, with **no decode step anywhere in the firmware**.

- `res/mkraw.py` decodes the source PNGs (stdlib `zlib` only — no Pillow/ImageMagick here) into
  flat `res/raw/*.raw` + `manifest.json` recording dimensions/format/stride/size.
- **Everything is RGB565**, hi-byte-first (panel order), alpha composited over black. This is
  deliberate: the DMA can only *stream raw pixels* — it cannot expand 1bpp or blend fg/bg — so
  the on-flash bytes must already be what the panel consumes. Keeping the firmware-embedded step
  in the same format makes the eventual flash move a **pure relocation, not a reformat**.
- Font atlases are **self-describing**: a magenta `(255,0,255)` marker sits at each glyph cell's
  top-left corner, so marker spacing IS the advance and marker count IS the glyph count.
  `font_metrics()` derives the grid from them instead of hardcoding it. Markers pack as
  background. (Regular-30: 95 glyphs @ 15x34; Medium-20: 95 @ 10x23, first char 0x20.)
- `mkraw.py --embed` generates `res/lcd_assets.c/.h` for the **interim** firmware-embedded stage.
  The full atlases (97KB + 44KB) can't fit alongside firmware in 256KB, so fonts are **subset**
  to `EMBED_CHARSET` (`0123456789:/%`). The full atlases stay in `raw/` for flash; subsetting is
  purely an embed-time concern and disappears once assets live in flash.
- Glyph colours are consequently **baked** — free here, since the dashboard is uniformly
  `COL_FG 0xFFFF` on `COL_BG 0x0000`. A second colour would mean a second atlas.

> **Gotcha:** `EMBED_CHARSET` is deliberately tight. Any dashboard text using characters outside
> it silently draws nothing — widen the charset and regenerate.

## Stage D as built (done)
`mkraw.py --flash` packs everything into one blob written at `FLASH_ASSET_BASE`
(`0x0CE0000`, chosen because it is the 3.12 MB that has read `0xFF` since
manufacture): a 4K index sector, then the assets page-aligned. Entry offsets are
region-relative, so the blob can be relocated.

Things that were NOT obvious going in:
- **Byte order is per draw path.** The DMA runs the SPI in 16-bit mode
  (`CTRL0.DL=0xF`), which swaps each byte pair, so on-flash pixels must be
  lo-byte-first -- the opposite of the hi-byte-first RAM tiles. Moving an asset
  to flash is a **reformat, not a relocation**, contrary to what this doc and
  `mkraw.py` used to claim.
- **Font atlases had to be repacked.** A glyph cell inside an atlas is strided
  (`cell_w` wide, `img_w` apart) and the DMA can only stream consecutive bytes,
  so an atlas is undrawable by it. Fonts are stored as per-glyph contiguous
  tiles; glyph *n* is one flat blit at `off + n*cell_w*cell_h*2`.
- **Nothing in the HID path may block.** A page program is ~1-3 ms and a sector
  erase 50-300 ms; a single synchronous erase cost ~6% of a scan window, and a
  one-shot 184 KB CRC verify dropped the scan from 1396 Hz to ~300 Hz. Commands
  return `FS_BUSY` and the host re-sends; the CRC folds ~1 KB per call and
  returns `FS_MORE`. (Chunking fixes *latency*, not throughput -- the verify
  still costs its total CPU, now spread out.)
- **Writes are policed in firmware, not trusted to the host.** The stock LCD
  assets below `0x1AA000` are never writable (our only dump of them has read
  damage); the stock animation slots need an explicit unlock.

`EMBED_CHARSET` and its "letters silently draw nothing" trap are gone -- both
full 95-glyph atlases are in flash.

**Consequence:** an unprovisioned keyboard shows a BLANK panel. There is no
embedded art left to fall back on. The console prints the provisioning command.

## Remaining work
- Nothing blocking. Optional: pipeline `spi1_rw` for bulk flash reads the way
  `tx_pipe` pipelines writes -- the verify path is byte-at-a-time and costs
  ~0.5 s of CPU for 184 KB.

## Staged plan (updated)
- **Stage A — DONE.** Bare-metal `lcd_bus` (SPI0 + IRQ + SPI1), pipelined `tx_bulk`, fills,
  full-frame flash DMA, animation player.
- **Stage B — DONE.** Dashboard coexists with the DMA (three branches; see comparison doc).
- **Stage C — DONE** (`ak820pro-flashlcd-tiles`). Pipelined bulk writer; `lcd_blit_ram` +
  `lcd_blit_flash` generalised to a rect; the whole dashboard moved onto pre-rendered RGB565
  tiles; **QGF and QFF decoders deleted** (~110 lines: block walk, byte-RLE, HSV palette, glyph
  offset table, `lerp565`). `lcd_draw_text()` lost its fg/bg args. Firmware 131KB of 256KB.
- **Stage D — DONE.** Flash-resident assets + provisioning toolchain (above). 58 KB reclaimed.

## Performance note (concrete, measured)
- Our **pipelined `tx_bulk`** streams at ~wire speed (24 MHz, ~3 MB/s) — a full frame ~13 ms.
- The **stock ChibiOS SN32 SPI** driver transfers **one byte per interrupt**, ~10× slower for
  bulk pixels — this is why `-qp-lld`'s full dashboard redraw dips the scan to ~700 Hz. It is
  driver overhead, **not** the SPI clock (divisor 2 already maps to clkdiv 0 = 24 MHz via a
  fall-through in `spi_master.c`'s SN32 divisor switch). See `docs/LCD_DMA_BRANCHES.md`.
