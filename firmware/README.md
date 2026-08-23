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
  main UI loop alone applies the latest `ClockValues` snapshot to LVGL.
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
tested. The newly connected Home Assistant/Open-Meteo data worker still needs
a physical smoke test.

The integrated upstream code remains covered by the MIT licenses in the two
submodules. Preserve both license files when distributing combined binaries.
