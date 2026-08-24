# Multi-mode screen technical prototype

This firmware is an integration prototype for the two pinned upstream
submodules. Small integration seams are kept explicit in those pinned source
trees. The prototype has one
LVGL/display/touch owner, a compile-time screen registry, stable screen IDs and
a central gesture recognizer.

Current gesture contract:

- horizontal swipe: previous/next screen;
- screens never change automatically on a timer;
- vertical swipe: delivered to the active module (the radar module changes
  its range);
- tap and long press: delivered to the active module/LVGL controls.

The radar acknowledges a vertical range swipe immediately with a short LVGL
overlay containing the direction and newly selected range. It is flushed before
the upstream renderer recalculates its cached crops or starts a RainViewer tile
burst, so slow data work cannot make a registered gesture look ignored.

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
- A complete Home Assistant refresh reuses one HTTP/TLS client across all
  configured entities. Transport failures stop the current batch, keep the
  last complete values and back off exponentially for 5--60 seconds. This
  avoids the former per-entity handshake bursts when the fragmented internal
  heap has ample total memory but no large contiguous block.
- `meteo.radar` now wraps the pinned MeteoPlaneRadar `ScreenWeather` renderer,
  CHMI animation client, RainViewer tile client, PNG decoding, European borders,
  city labels and source status model. The original renderer draws into a
  host-owned 480x480 RGB565 PSRAM canvas; LVGL presents that canvas through the
  sole display pipeline. Its location, source and five-step range model come
  from the canonical MeteoPlaneRadar `Settings` implementation, and vertical
  range changes retain the original debounced `planeradar` NVS state. This path
  has passed a physical CHMI smoke test; RainViewer still needs a separate
  physical smoke test.
- `meteo.forecast` wraps the pinned MeteoPlaneRadar `Forecast`,
  `ScreenForecast` and `WxIcon` implementations. It uses the same shared
  480x480 canvas as radar, retains the original Open-Meteo forecast and
  air-quality parsing, and performs network work only while visible under the
  common fetch lease. Its hardware smoke test is still pending.

The host configuration is stored as versioned JSON in NVS. Schema 3 uses stable
screen IDs, enabled flags and configured order, always keeps at least one
screen reachable, appends newly introduced built-in screens without disturbing
an existing order, and deliberately omits timed rotation.
Clock settings retain the original binary schema, checksum,
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
CHMI/RainViewer/Open-Meteo clients so multiple TLS sessions cannot compete for
the ESP32-S3 internal heap. A CHMI download owns the lease for its complete
frame batch. RainViewer owns it for the active incremental tile burst and
releases both the lease and reusable TLS connection when the screen is hidden.
Forecast owns it for each complete upstream request-and-parse batch.

Radar and forecast share one host-owned `meteo_canvas` PSRAM allocation and
select its presentation callback from their `show()`/`hide()` lifecycle. This
keeps the upstream global `gfx` contract intact without allocating another
roughly 460 kB surface or introducing another panel flush owner.

The standalone Meteo build retains Arduino_GFX 1.4.9 through its own sketch
profile. The combined firmware pins Arduino_GFX 1.6.6 because its Arduino-ESP32
3.3.11 framework uses the newer SPI API; the renderer-facing Arduino_GFX API is
unchanged. PNGdec remains pinned to 1.0.1, and the upstream draw callbacks now
use that library's declared `void` callback signature. CHMI and RainViewer share
one decoder because they are mutually exclusive sources. Its roughly 48 kB
working object is allocated in PSRAM, preserving scarce contiguous internal
RAM for TLS while keeping the source frame buffers separate.

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
