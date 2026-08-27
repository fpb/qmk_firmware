# AK820 Pro — the LCD branches (flash→LCD DMA dashboard)

Every LCD branch renders the same dashboard: pre-rendered RGB565 tiles (and GIF animations)
streamed straight off the external SPI flash by the SN32 **SPI1(flash)→SPI0(panel) DMA** — no art
baked into firmware; it is provisioned to flash from the host with `ak820ctl`. The branches differ
in two axes:

1. **How the SPI buses are driven** — bare-metal register pokes vs. the ChibiOS SN32 SPI driver.
2. **(earlier branches only) how a Quantum Painter dashboard coexists with the DMA** — the
   corruption that appears when QP and the non-blocking DMA both touch SPI0.

**Preferred branch: [`ak820pro-flashlcd-unified-dualspi`](https://github.com/fpb/qmk_firmware/tree/ak820pro-flashlcd-unified-dualspi)** — both SPI buses under the ChibiOS driver, no QP.

They fall into two lines of development:

- **Flash-tile line (current)** — no Quantum Painter; the dashboard is pre-rendered tiles.
  `-tiles` (bare-metal) → `-unified` (panel on the driver) → **`-unified-dualspi`** (panel *and*
  flash on the driver).
- **QP-coexistence line (earlier)** — Quantum Painter kept for its drawing/asset richness, with
  three different answers to the QP-vs-DMA collision. `ak820pro-flashlcd`, `-qp`, `-qp-lld`.

---

## Flash-tile line (current)

No QP, no qgf/qff decoders — every visual is a pre-rendered RGB565 tile, exactly the format the
DMA streams. The three branches differ only in how much of the SPI path is the ChibiOS driver.

|                        | `ak820pro-flashlcd-tiles` | `ak820pro-flashlcd-unified`      | `ak820pro-flashlcd-unified-dualspi` (**preferred**) |
|------------------------|---------------------------|----------------------------------|------------------------------------------------------|
| SPI0 (panel) owner     | Bare-metal (we own it)    | **ChibiOS SPI driver** (`spiSend`) | **ChibiOS SPI driver** (`spiSend`)                 |
| SPI1 (flash) owner     | Bare-metal                | Bare-metal                       | **ChibiOS SPI driver** (`spiExchange(&SPID1)`)      |
| flash→LCD DMA          | Bare-metal `Vector58`     | Driver extension `spiSN32FlashDma*` (borrows SPI0) | Driver extension, **borrows SPI0 + SPI1** |
| `HAL_USE_SPI`          | FALSE                     | TRUE                             | TRUE                                                 |
| Register-level flash code | all of it              | flash bus + DMA                  | only the irreducible P2P-DMA / board bits            |

### `ak820pro-flashlcd-tiles` — leanest, fully bare-metal
Owns both buses directly; no `HAL_USE_SPI`. Every visual is a pre-rendered tile blitted straight to
the panel — no decoding at all (no block walk, no byte-RLE, no HSV palette, no `lerp565`); text is
one tile blit per character from a monospace atlas indexed by `strchr(charset, c)`. Assets are
generated from the source PNGs by `res/mkraw.py` (see `LCD_FLASH_LAYER.md`). ~0.5 KB smaller than
`-unified` and with no ChibiOS SPI dependency — the choice when you want the leanest build.

### `ak820pro-flashlcd-unified` — panel on the ChibiOS driver (SPI0-only-driver predecessor)
The `-tiles` dashboard, but SPI0 goes through the ChibiOS SN32 SPI driver: all panel commands and
pixels via `spiSend` (FIFO-batched by `spi_fifo_pump.diff`), and the flash→LCD DMA is the driver's
`spiSN32FlashDma*` extension (`spi_flash_dma.diff`) that borrows SPI0 for the data phase. **SPI1
(flash) stays bare-metal.** Using the standard driver keeps the panel path maintainable and leaves
QP available if wanted, at +468 bytes and no measurable speed loss vs `-tiles`. **Superseded by
`-dualspi`**; kept as the more battle-tested fallback.

### `ak820pro-flashlcd-unified-dualspi` — both buses on the driver (**preferred**)
`-unified` plus the SPI1 migration: all CPU-path flash I/O (index read, JEDEC, status, write-enable,
page program, sector erase) goes through `spiSend`/`spiExchange(&SPID1)`, and the flash→LCD DMA
extension borrows **both** instances for the transfer. The only register-level flash code left is the
irreducible SN32 peripheral-to-peripheral DMA / board specifics (SPI1 PFPA mux, EBI/LCD DMA clocks,
the blit's `READ`+addr command phase) — P2P DMA is outside the `hal_spi` buffer model, so *some*
extension is unavoidable.

**Key SN32 detail:** to keep the driver's SPI1 handler from racing the DMA (which reads the SPI1 RX
FIFO directly), the extension **disables SPI1's NVIC vector** for the DMA window — *not* by masking
`RXFIFOTHIE`, because on the SN32 that same RX-threshold event is what triggers the DMA request;
clearing it stalls the transfer. Completion flushes the flash FIFO + pending flag and re-enables the
vector. Hardware-validated: driver-based flash read, erase, program and CRC verify, plus the
dashboard/animation DMA blits; matrix scan steady at ~1392 Hz (full-redraw dip ~−2%).

---

## QP-coexistence line (earlier)

These keep Quantum Painter and each answer *"when QP and the DMA both want SPI0, how do we keep them
apart?"* differently. They all use the **same** SPI1→SPI0 DMA for the animation.

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

### `ak820pro-flashlcd` — shipped default
QP is gone. `lcd_bus.c` is a full bare-metal GC9107 driver: it owns SPI0, draws the dashboard
with its own primitives (`lcd_fill_rect`, `lcd_draw_qgf`, `lcd_draw_text`), and decodes QP's
qgf/qff blobs in-house. The animation DMA and its `Vector58` completion ISR are all ours. **No
coexistence problem exists** because nothing else touches SPI0. Simplest, self-contained — the cost
is that you maintain the qgf/qff decoders. (`-tiles` above descends from this by dropping the
decoders too.)

### `ak820pro-flashlcd-qp` — QP over our bare-metal comms
QP is back for its drawing/asset richness, but it talks to the panel through *our* comms vtable
(`qc_*` → pipelined `tx_bulk`), and we still own SPI0 + the bare-metal `Vector58`. Coexistence
fix is surgical and **deterministic**: `qc_start()` returns false (and `qc_stop()` skips raising
CS) while `anim_active()`, so QP is categorically blocked from the bus during the animation.
**Fastest QP**, no core/submodule patches — but it couples to QP's *internal* API (comms/
tft_panel vtables, `qp_internal_register_device`), so it is the most fragile against upstream QP
churn.

### `ak820pro-flashlcd-qp-lld` — QP over the stock driver + Vector58 tunnel
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
patches (LLD hook + QP macro), both opt-in / harmless when their macros are undefined. (`-qp-lld`'s
driver-owned DMA extension later became the `spiSN32FlashDma*` extension the `-unified` line uses.)

---

## Choosing
- **Default / preferred** → `ak820pro-flashlcd-unified-dualspi`. Both buses on the ChibiOS driver,
  no QP, hardware-validated flash read/write + DMA.
- **Battle-tested fallback** → `ak820pro-flashlcd-unified`. Same, but flash (SPI1) still bare-metal.
- **Leanest** → `ak820pro-flashlcd-tiles`. No QP, no decoders, no `HAL_USE_SPI`.
- **Want QP + best performance** → `ak820pro-flashlcd-qp`. Deterministic gate, no external
  patches; cost is coupling to QP internals.
- **Want QP "as-provided" / minimal QP-side custom code** → `ak820pro-flashlcd-qp-lld`. Stock
  driver, but you accept slower QP and two upstream patches.
- **Shipped / QP with in-house decoders** → `ak820pro-flashlcd`.

## Historical note
The QP coexistence corruption (top-rows garbage / freeze whenever the DMA ran non-blocking with QP
enabled) was root-caused to `qp_internal_task()` → `qp_flush()` bracketing its no-op tft_panel
flush with `qp_comms_start()/stop()`, which drives panel **CS (B8)** low then HIGH — deselecting
the panel mid-DMA. It was fixed-in-time (~1 ms), not byte-count, which is why slower DMA clocks
wrote fewer lines and why `display_set_paused` never helped (it only stopped OUR flush, not
`qp_internal_task`'s). The `-qp`/`-qp-lld` branches are the clean resolutions of that; the
flash-tile line sidesteps it entirely by dropping QP.
