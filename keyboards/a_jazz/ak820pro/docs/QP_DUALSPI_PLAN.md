# Plan — QP dashboard on the dualspi SPI base (and the two-backend endgame)

> **Status: done.** This plan was realised on `ak820pro-qp-dualspi`, which now
> carries both backends behind `-e DASHBOARD_BACKEND=custom|qp`. The two intermediate
> single-backend branches it references below have been retired (folded in). Kept as the
> design record.

**Goal:** the dualspi SPI solution (SPI0+SPI1 on the ChibiOS driver, flash→LCD DMA extension
borrowing both) as the shared base of two dashboard variants:
1. **DMA-only / custom** — bare-metal tile dashboard (now the `custom` backend).
2. **QP** — Quantum Painter dashboard + splash + the same DMA animation (now the `qp` backend).

## Phase 1 (this branch) — get QP working on the dualspi base
Port qp-lld's QP rendering onto the dualspi SPI base:
- **From qp-lld:** `graphics/display.*`, `graphics/lcd_bus.*` (QP-owns-SPI0 + borrow model),
  `graphics/res/*.qgf|.qff`, `ak820pro.c`, `rules.mk` (`QUANTUM_PAINTER_ENABLE` + `gc9107_spi`),
  `halconf.h`, the QP config defines, and the `quantum/painter/qp_internal.c` task-disable edit
  (`QUANTUM_PAINTER_INTERNAL_TASK_DISABLE`).
- **Keep from dualspi:** `mcuconf.h` (`SN32_SPI_USE_SPI1 TRUE`), `spi_flash_dma.diff`
  (borrows SPI0+SPI1, NVIC-vector isolation), `keyboard.json`/`via.json`, keymaps.
- **Merge in `lcd_bus.c`:** apply dualspi's SPI1→driver change to qp-lld's bus — flash I/O via
  `spiExchange(&SPID1)`, `spiStart(&SPID1)`, and update the DMA calls to the new
  `spiSN32FlashDmaPrepare(&SPID0, &SPID1, len)` signature (bare-metal `spi1_raw_byte` for the
  command phase, vector disabled during the DMA).
- Compile, then **hardware-validate:** QP dashboard/splash draws; DMA animation runs with QP
  paused (`QUANTUM_PAINTER_INTERNAL_TASK_DISABLE` stops the CS-toggle flush); flash access via
  SPID1 (note: QP dashboard art is EMBEDDED qgf/qff; flash is used for the animation slots and
  provisioning — this follows qp-lld's asset model, not the tile-index model).

## Phase 2 (after both validated) — collapse to one branch, two backends
Extract a link-time seam so a build flag picks the renderer:
```
dashboard_backend.h:  dash_init/rect/icon/text/splash/flush  + shared dash_anim_*
dashboard_qp.c        (qp_* )      dashboard_custom.c   (bare-metal tiles)
rules.mk:  DASHBOARD_BACKEND ?= custom   ->  qmk compile ... -e DASHBOARD_BACKEND=qp
```
Shared: dualspi SPI base + DMA animation + dashboard **layout logic** (`display.c`).
Backend-specific: rect/icon/text/splash rendering, panel init, asset encoding.
Recurring cost: dashboard/splash assets dual-encoded (qgf/qff vs raw tiles) behind one id;
animation frames are identical raw flash tiles for both.

Build the fork first, prove the seam on real code, THEN abstract (avoid a premature/leaky API).
