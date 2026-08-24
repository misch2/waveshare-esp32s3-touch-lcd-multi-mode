# Multi-mode screen technical prototype

This firmware is an integration prototype for the two pinned upstream
submodules. The upstream source trees stay unchanged. The prototype has one
LVGL/display/touch owner, a compile-time screen registry, stable screen IDs and
a central gesture recognizer.

Current gesture contract:

- horizontal swipe: previous/next screen;
- vertical swipe: delivered to the active module (the radar prototype changes
  its range);
- tap and long press: delivered to the active module/LVGL controls.

The clock dashboard additionally filters its manual day/night short-click
handler through the central recognizer's tap tolerance. This is necessary
because LVGL reports pointer release before the host dispatches the completed
swipe; a horizontal screen swipe must not also toggle the clock's night mode.

Implemented screens:

- `clock.dashboard` wraps the actual pinned `waveshare-hodiny` LVGL dashboard,
  fonts and static weather assets. It loads the original versioned
  `ClockConfig` (including migrations), reuses the upstream
  `clock-wifi`/Improv provisioning path and displays real SNTP time. Its
  background `ClockDataService` preserves the original Home Assistant and
  Open-Meteo clients, retry intervals, parsing and day/night behavior while the
  main UI loop alone applies the latest `ClockValues` snapshot to LVGL. The
  host-owned HTTP server exposes a common landing page at
  `http://waveshare-hodiny.local/`, while the original clock configuration,
  authentication and diagnostics are mounted at
  `http://waveshare-hodiny.local/clock/` with namespaced API routes. Automatic
  firmware discovery and installation are deliberately disabled in the
  combined build; updates are performed manually by flashing a locally built
  image.
- `meteo.radar` is still a lightweight LVGL renderer demonstrator and does not
  yet download precipitation frames. Its location, source and five-step range
  model now come from the canonical MeteoPlaneRadar `Settings` implementation;
  vertical range changes use the original debounced `planeradar` NVS state.

The host configuration is stored as versioned JSON in NVS. It uses stable
screen IDs, enabled flags and configured order, and always keeps at least one
screen reachable. Clock settings retain the original binary schema, checksum,
migrations and dedicated `clockcfg` partition. The combined partition table
retains two application slots for safe manual firmware deployment, but the
running firmware does not contact a release server or expose an automatic
update action.

Build from this directory with `pio run`. The first build may need to download
LVGL 8.3.10. This is deliberately a hardware prototype, not yet a replacement
for either upstream firmware.

Validated output is written to
`.pio/build/waveshare-prototype/firmware.factory.bin`. The original display and
gesture prototype, persisted clock settings, Wi-Fi and SNTP were hardware-
tested. Real Home Assistant values and the on-device settings overlay were also
verified. The host landing page, prefixed clock routes, HTTP configuration
persistence and resulting display updates were verified on the physical device
as well.

The clock web module now accepts the host-owned `WebServer`. The combined
firmware mounts it at `/clock/` and `/api/modules/clock/*`; the standalone clock
firmware retains its original `/` and `/api/*` aliases. The original HTML,
serialization, validation, password/session handling and export/import logic
remain upstream. Do not mount the colliding Meteo routes unchanged.

The first production Meteo seam is connected independently of rendering.
`MeteoRadarConfig` is a fixed-size, host-testable snapshot rather than another
settings store. `meteo_settings` compiles the pinned upstream `Settings` and
language implementations through thin translation units. Runtime NVS writes
use the same display stop/recreate transaction as clock configuration writes.
A host-owned fetch mutex now serializes the existing clock data worker and the
future CHMI/RainViewer clients so multiple TLS sessions cannot compete for the
ESP32-S3 internal heap.

The integration display host selects an 8 MHz RGB pixel clock and waits for
VSYNC before LVGL may reuse a flushed framebuffer. This is intended to prevent
short repeated/shifted horizontal row blocks under full-refresh load. This base
timing change has passed a hardware smoke test. Configuration saves never use
the in-stream RGB DMA restart: hardware testing showed that even a
VSYNC-scheduled restart could leave the complete framebuffer cyclically shifted
by several rows until another restart. A reset/init of the existing driver also
failed because it did not stop GDMA or discard the driver's bounce state. The
current Save transaction therefore deletes the complete RGB driver before NVS,
then creates a new driver, framebuffers, DMA descriptors and bounce buffers and
rebinds LVGL afterwards. This full lifecycle, including Save through `/clock/`,
passed repeated physical testing. Watch the serial log for
`LCD VSYNC timeout during ...` warnings after future display changes.

The root cause is flash/cache interaction, not the stored values themselves.
ESP-IDF explicitly states that a PSRAM framebuffer with bounce buffers cannot
operate while NVS disables the external-memory cache. Resetting the existing
panel handle did not stop GDMA and did not reproduce a cold start; physical
testing still produced a persistent vertical wrap after Save. The integration
briefly tested direct double-framebuffer PSRAM/EDMA scanout without bounce
buffers. That avoided the cache-dependent refill ISR, but caused horizontal
jitter on every refresh, faster on the radar screen, so the experiment was
reverted. The proven normal-redraw baseline remains the upstream 20-line bounce
buffer with 8 MHz PCLK and the VSYNC flush gate. Only configuration persistence
uses the full delete/recreate transaction. The combined PlatformIO build and
physical display tests passed with this lifecycle.

The integrated upstream code remains covered by the MIT licenses in the two
submodules. Preserve both license files when distributing combined binaries.
