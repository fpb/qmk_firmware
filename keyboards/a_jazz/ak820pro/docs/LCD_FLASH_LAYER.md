# AK820 Pro — flash-backed LCD layer (design + roadmap)

## Vision
Move *all* LCD drawing onto the SN32 SPI-to-SPI DMA: every visual (animation frame,
image, and eventually text) is a **tile of RGB565 pixels stored in the external SPI
flash**, blitted straight to the panel by hardware DMA with zero CPU in the data path.
Text becomes composition of **fixed-size glyph tiles**. Quantum Painter stays only as
a transitional renderer for anything not yet flash-backed.

## Why this shape
- On this Cortex-M0 (no VTOR) the SPI0 IRQ (`Vector58`) has one owner, fixed at link
  time. Interrupt-driven DMA requires us to own it → `SN32_SPI_USE_SPI0 = FALSE` and we
  drive SPI0 bare-metal. QP then renders through our bus too (see the QP shim below).
- The DMA is the only path that doesn't block the (single-threaded) matrix scan for the
  transfer: a full 128×128 frame is ~13 ms of hardware transfer with the CPU free.

## The one primitive everything is built on
```c
// Blit a w×h RGB565 tile from flash offset `src` to the panel rect at (x,y).
// Interrupt-driven (Vector58): returns immediately; completion via callback/flag.
// Full-frame animation, images, and glyph tiles are ALL just calls to this.
void lcd_blit_flash(uint32_t src, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
```
Generalising the current full-frame blit to an arbitrary rect is small: `DMACNT =
w*h*2 - 1`, window = `(x, y, x+w-1, y+h-1)`, `src` arbitrary. Everything else (16-bit
TX, DFETCH, N-1 count, drain-before-CS) stays as proven in SPI_DMA_FINDINGS.md.

## Layered architecture
```
 Application: dashboard, animations, future flash-native screens
      |                                  |
 QP (transitional: text/vector)     flash draws: images, glyphs, animations
      |  (custom comms shim)             |  (lcd_blit_flash)
      +-------------- LCD bus: bare-metal SPI0, we own Vector58 --------------+
                  |                                   |
        pipelined CPU writes                 SPI-to-SPI DMA (IRQ-driven)
        (QP flush, fills, commands)          (flash tile -> LCD rect)
```

## Modules (target)
- `lcd/lcd_bus.*`   — SPI0 bring-up + `Vector58` + SPI1 (flash). Owns the bus.
- `lcd/lcd_draw.*`  — `lcd_window`, **pipelined** bulk pixel write (FIFO-kept-full),
                      `lcd_fill`, command/data helpers. Used by QP shim + fills.
- `lcd/lcd_blit.*`  — `lcd_blit_flash(src,x,y,w,h)` + IRQ completion + a small blit
                      queue (so composited screens can enqueue several tiles).
- `lcd/qp_comms.*`  — QP comms vtable routed to `lcd_draw` (keeps the dashboard while
                      we transition). Delete once QP is no longer needed.
- `graphics/anim.*` — animation player: a sequence of `lcd_blit_flash` full-frames.

## Extension points (the "later" work — additive, no refactor)
- **Flash asset index.** A small table (id → flash addr, w, h, [frame count, durations]).
  We already decoded the on-flash header format: `[count][per-frame descriptor][pad]`,
  frames at `hdr+0x100`, 0x8000 each (see [[ak820pro-flash-animation-layout]]).
  `lcd_draw_image(id, x, y)` = lookup → `lcd_blit_flash`.
- **Fixed-size flash font.** Store the glyph set as equal-size tiles (e.g. 16×24) in
  flash. `lcd_draw_char(c, x, y)` = `glyph_base + (c - first)*tile_bytes` →
  `lcd_blit_flash`. `lcd_draw_text` advances x by the tile width. Kerning/AA optional.
- **Authoring/loader.** A host tool converts QP images/fonts to RGB565 tiles and writes
  them to flash (the AJAZZ loader format is partially decoded; or a custom region).

## Performance note (why QP-over-our-bus is fine)
Both QP-over-ChibiOS-SPI and QP-over-our-bus are SPI-clock-limited and block the scan
for the flush duration (single-threaded). With a **pipelined** bulk writer our bus
matches the interrupt driver's throughput; small dashboard updates are sub-ms either
way. See the perf discussion in the project notes.

## QP-over-our-bus: the exact integration seam (traced)
- `qp_gc9107_make_spi_device` fills a generic `tft_panel_dc_reset_painter_device_t`
  (`qp_tft_panel`) and sets `base.comms_vtable = &spi_comms_with_dc_vtable`,
  `base.comms_config = &spi_dc_reset_config`. The gc9107 `driver_vtable` (init seq +
  MADCTL) and `qp_tft_panel` (viewport/pixdata) are **comms-agnostic** — they call
  `qp_comms_*` which dispatch through `comms_vtable`.
- So: provide our own `painter_comms_with_command_vtable_t` (`comms_init/start/stop/
  send` + `send_command`/`bulk_command_sequence`) that talks to `lcd_bus`, and build a
  `tft_panel_dc_reset_painter_device_t` that uses it. QP's fonts/images/layout are
  unchanged; only the pixel/command transport becomes our bus.
- Build implication: do NOT enable `QUANTUM_PAINTER_GC9107_SPI` (that pulls in
  `qp_comms_spi` -> `spi_master` -> ChibiOS SPI, which forces `SN32_SPI_USE_SPI0=TRUE`
  and steals `Vector58`). Instead register the tft_panel device directly with our
  comms. Then `SN32_SPI_USE_SPI0=FALSE`, `HAL_USE_SPI=FALSE`, and we own `Vector58`.
- Byte stream is identical to today's (same gc9107 driver_vtable), so colors/format
  carry over; our `comms_send` just needs a FIFO-pipelined bulk write for speed.

## Staged build plan
- Stage A: `lcd_bus` (SPI0 + Vector58 + SPI1) + `lcd_draw` (window, pipelined bulk
  write, fills) + generalized interrupt-driven `lcd_blit_flash(src,x,y,w,h)`. Validate
  with the animation (QP temporarily off, or a minimal bare-metal dashboard).
- Stage B: custom QP comms vtable over `lcd_bus` + register a tft_panel gc9107 device
  with it; drop the gc9107_spi driver; `SN32_SPI_USE_SPI0=FALSE`. Dashboard back, now
  over our bus, with interrupt-driven DMA available for animations.
- Stage C: flash images + fixed-size flash font as additive callers of `lcd_blit_flash`.

## Status / next step
- DMA blit + Mario animation proven on branch `ak820pro-full-bitblt-standalone`
  (owns SPI0/Vector58, QP off). Flash format decoded.
- The poll-based coexistence attempt (this branch, early) is a dead end — replace with
  the bare-metal-bus + IRQ-DMA foundation above, generalise the blit to a rect, add the
  QP comms shim so the dashboard survives. Then images and flash-text are additive.
