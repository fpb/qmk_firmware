# AK820 Pro — the three LCD branches with working flash→LCD DMA animations

All three branches fork from `5d84e36b66` and share every non-rendering file (matrix, RGB,
RTC, wireless). They differ **only** in how the LCD is driven and how the flash→LCD DMA
animation coexists with the QP dashboard. All three use the **same** SN32 SPI1(flash)→SPI0(LCD)
DMA for the animation; the only question each answers differently is *"when QP and the DMA both
want SPI0, how do we keep them apart?"*

## At a glance

|                          | `ak820pro-flashlcd` (shipped) | `ak820pro-flashlcd-qp`        | `ak820pro-flashlcd-qp-lld`            |
|--------------------------|-------------------------------|------------------------------|---------------------------------------|
| Quantum Painter          | **Removed** entirely          | Linked, **custom comms**     | Linked, **stock driver**              |
| SPI0 (LCD) owner         | Bare-metal (we own it)        | Bare-metal (we own it)       | **ChibiOS SPI driver** (`SN32_SPI_USE_SPI0=TRUE`) |
| Dashboard drawn by       | Our `lcd_*` primitives        | QP → our `qc_*` comms        | QP → stock `gc9107_spi`               |
| Asset formats            | Our qgf/qff **decoders**      | QP's qgf/qff (native)        | QP's qgf/qff (native)                 |
| DMA completion IRQ       | Our bare-metal `Vector58`     | Our bare-metal `Vector58`    | **Weak hook on the driver's `Vector58`** (tunnel) |
| Coexistence mechanism    | N/A (no QP to collide)        | Gate `qc_start/qc_stop` on `anim_active()` | Disable `qp_internal_task` (macro) |
| Patches outside kb folder| none                          | none                         | **2**: LLD tunnel hook + QP macro     |
| QP redraw speed          | n/a                           | fastest (pipelined `tx_bulk`)| slowest (stock: 1 byte / interrupt)   |

## The branches

### 1. `ak820pro-flashlcd` — shipped default
QP is gone. `lcd_bus.c` is a full bare-metal GC9107 driver: it owns SPI0, draws the dashboard
with its own primitives (`lcd_fill_rect`, `lcd_draw_qgf`, `lcd_draw_text`), and decodes QP's
qgf/qff blobs in-house. The animation DMA and its `Vector58` completion ISR are all ours. **No
coexistence problem exists** because nothing else touches SPI0. Simplest, leanest, fully
self-contained — the cost is that you maintain the qgf/qff decoders.

### 2. `ak820pro-flashlcd-qp` — QP over our bare-metal comms
QP is back for its drawing/asset richness, but it talks to the panel through *our* comms vtable
(`qc_*` → pipelined `tx_bulk`), and we still own SPI0 + the bare-metal `Vector58`. Coexistence
fix is surgical and **deterministic**: `qc_start()` returns false (and `qc_stop()` skips raising
CS) while `anim_active()`, so QP is categorically blocked from the bus during the animation.
**Fastest QP**, no core/submodule patches — but it couples to QP's *internal* API (comms/
tft_panel vtables, `qp_internal_register_device`), so it is the most fragile against upstream QP
churn.

### 3. `ak820pro-flashlcd-qp-lld` — QP over the stock driver + Vector58 tunnel
The "QP exactly as shipped" approach: the **ChibiOS SPI driver** owns SPI0 and QP uses the stock
`gc9107_spi` driver untouched. Because the driver owns `Vector58`, our DMA completion is serviced
by a **weak hook** patched into `hal_spi_v2_lld.c` (enabled by `SN32_SPI0_DMA_TUNNEL` in
mcuconf.h); during an animation we borrow SPI0 from the driver and restore its registers after.
With no `qc_*` to gate, the **deterministic** fix is the opt-in core macro
`QUANTUM_PAINTER_INTERNAL_TASK_DISABLE` (early-return in `quantum/painter/qp_internal.c`), which
stops `qp_internal_task`'s periodic `qp_flush()` CS toggle at the source — safe here because the
framebuffer-less tft_panel draws immediately, so `qp_flush()` was a no-op except that CS toggle.
**Slowest QP** (the stock ChibiOS SN32 SPI transfers one byte per interrupt → the ~700 Hz scan
dip on the one full dashboard redraw when toggling the animation OFF; this is driver overhead,
**not** the SPI clock — divisor 2 already maps to clkdiv 0 = 24 MHz). Carries **two** out-of-tree
patches (LLD hook + QP macro), both opt-in / harmless when their macros are undefined.

## Choosing
- **Ship / simplest** → `ak820pro-flashlcd`. No QP, no patches, non-blocking animation.
- **Want QP + best performance** → `ak820pro-flashlcd-qp`. Deterministic gate, no external
  patches; cost is coupling to QP internals.
- **Want QP "as-provided" / minimal QP-side custom code** → `ak820pro-flashlcd-qp-lld`. Stock
  driver, but you accept slower QP and two upstream patches.

## Historical note
The coexistence corruption (top-rows garbage / freeze whenever the DMA ran non-blocking with QP
enabled) was root-caused to `qp_internal_task()` → `qp_flush()` bracketing its no-op tft_panel
flush with `qp_comms_start()/stop()`, which drives panel **CS (B8)** low then HIGH — deselecting
the panel mid-DMA. It was fixed-in-time (~1 ms), not byte-count, which is why slower DMA clocks
wrote fewer lines and why `display_set_paused` never helped (it only stopped OUR flush, not
`qp_internal_task`'s). All three branches above are the clean resolutions of that.
