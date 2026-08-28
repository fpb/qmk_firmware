# AK820 Pro — the LCD dashboard branches

The LCD dashboard is rendered from pre-rendered RGB565 tiles (custom backend) or embedded
qgf/qff assets (QP backend), and animations are streamed straight off the external SPI flash by
the SN32 **SPI1(flash)→SPI0(panel) DMA** (`spiSN32FlashDma*` extension). No animation art is
baked into firmware; it is provisioned to flash from the host with `ak820ctl`.

## Current branches

| Branch | What it is |
|--------|-----------|
| **`ak820pro-lcd-flash`** (preferred) | Two dashboard renderers on one **dualspi** SPI base (both SPI buses through the ChibiOS SN32 SPI driver), chosen at build time: `-e DASHBOARD_BACKEND=custom` (default, bare-metal RGB565 tiles from external flash) or `-e DASHBOARD_BACKEND=qp` (Quantum Painter dashboard + splash from embedded qgf/qff via the stock `gc9107_spi` driver). Both share the flash layer (`graphics/flash_io.c` — SPID1 reads + `ak820ctl` provisioning) and the animation DMA; they differ only in the SPI0/panel path, renderer, and asset encoding (gated by `DASHBOARD_BACKEND` + the `SN32_SPI0_FLASH_DMA_DRIVER_RESIDENT` mcuconf macro). |
| `ak820pro-lcd-embedded` | Static QP dashboard with **all art embedded in firmware**; no external flash, no animations, 4 submodule patches. The simplest QP build. |

## SN32 gotchas worth remembering
- The flash→LCD DMA is peripheral-to-peripheral (SPI1 RX → SPI0 TX); it does not fit the
  `hal_spi` buffer API, hence the `spiSN32FlashDma*` driver extension.
- Do **not** mask SPI1's `RXFIFOTHIE` to quiet its driver handler during the DMA — that bit
  also gates the RX-threshold event that *triggers* the DMA. Disable the NVIC vector instead.
- The DMA completion's SPI0 FIFO-IRQ restore is gated by `SN32_SPI0_FLASH_DMA_DRIVER_RESIDENT`:
  on for the custom backend (drives the driver directly between DMAs), off for QP (which
  re-runs `spiStart` per flush — leaving it on crashes QP via a stray RX-threshold dispatch).

## Historical note (Quantum Painter coexistence)
Earlier QP-over-driver experiments hit a corruption (top-rows garbage / freeze) whenever the
non-blocking DMA ran with QP enabled. Root cause: `qp_internal_task()` → `qp_flush()` bracketed
its no-op tft_panel flush with `qp_comms_start()/stop()`, toggling panel **CS (B8)** low→high and
deselecting the panel mid-DMA. It was fixed-in-time (~1 ms), not byte-count. The QP backend
suppresses it deterministically with `QUANTUM_PAINTER_INTERNAL_TASK_DISABLE` (a small
`quantum/painter/qp_internal.c` early-return). The custom/tile backend sidesteps it entirely by
not linking QP.
