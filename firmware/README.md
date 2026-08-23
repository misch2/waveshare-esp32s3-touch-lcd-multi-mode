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

Implemented screens:

- `clock.dashboard` wraps the actual pinned `waveshare-hodiny` LVGL dashboard,
  fonts and static weather assets. It loads the original versioned
  `ClockConfig` (including migrations), reuses the upstream
  `clock-wifi`/Improv provisioning path and displays real SNTP time. Its
  background `ClockDataService` preserves the original Home Assistant and
  Open-Meteo clients, retry intervals, parsing and day/night behavior while the
  main UI loop alone applies the latest `ClockValues` snapshot to LVGL. The
  original clock web configuration, authentication and diagnostics are exposed
  by the sole HTTP server at `http://waveshare-hodiny.local/`.
- `meteo.radar` is a lightweight LVGL radar demonstrator. It proves vertical
  range gestures and host navigation, but does not yet download or render the
  MeteoPlaneRadar data.

The host configuration is stored as versioned JSON in NVS. It uses stable
screen IDs, enabled flags and configured order, and always keeps at least one
screen reachable. Clock settings retain the original binary schema, checksum,
migrations and dedicated `clockcfg` partition. The combined partition table
retains standard OTA offsets.

Build from this directory with `pio run`. The first build may need to download
LVGL 8.3.10. This is deliberately a hardware prototype, not yet a replacement
for either upstream firmware.

Validated output is written to
`.pio/build/waveshare-prototype/firmware.factory.bin`. The original display and
gesture prototype, persisted clock settings, Wi-Fi and SNTP were hardware-
tested. Real Home Assistant values and the on-device settings overlay were also
verified. The HTTP configuration server, persistence and resulting display
updates were verified on the physical device as well.

The current web compatibility bridge intentionally retains the clock project's
original `/` and `/api/*` routes. Before adding the Meteo web UI it must be
refactored to accept host-owned `/clock/` and clock API prefixes; do not mount
the colliding Meteo routes unchanged.

The integration display host selects an 8 MHz RGB pixel clock and waits for
VSYNC before LVGL may reuse a flushed framebuffer. This is intended to prevent
short repeated/shifted horizontal row blocks under full-refresh load. This base
timing change has passed a hardware smoke test. Configuration saves never use
the in-stream RGB DMA restart: hardware testing showed that even a
VSYNC-scheduled restart could leave the complete framebuffer cyclically shifted
by several rows until another restart. Save uses a controlled stop, storage
write and full boot-style panel start, followed by a normal VSYNC-gated redraw.
This path was not compiled by Codex and still needs a hardware stress test;
watch the serial log for `LCD VSYNC timeout during ...` warnings.

The root cause is flash/cache interaction, not the stored values themselves.
The bundled framework cannot refill the 20-line bounce buffers from a PSRAM
framebuffer while NVS disables the external-memory cache. First-boot storage is
therefore initialized before the LCD starts. Runtime configuration saves briefly
blank and reset the panel, perform all associated NVS writes, then use the full
boot-style panel initialization to restore a known line origin before redrawing.

The integrated upstream code remains covered by the MIT licenses in the two
submodules. Preserve both license files when distributing combined binaries.
