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
  fonts and static weather assets. It currently uses deterministic demo values
  and an uptime-based clock because Wi-Fi, SNTP and Home Assistant are not part
  of this hardware spike yet.
- `meteo.radar` is a lightweight LVGL radar demonstrator. It proves vertical
  range gestures and host navigation, but does not yet download or render the
  MeteoPlaneRadar data.

The host configuration is stored as versioned JSON in NVS. It uses stable
screen IDs, enabled flags and configured order, and always keeps at least one
screen reachable. The combined partition table retains standard OTA offsets
and reserves the upstream clock configuration partition for the next phase.

Build from this directory with `pio run`. The first build may need to download
LVGL 8.3.10. This is deliberately a hardware prototype, not yet a replacement
for either upstream firmware.

Validated output is written to
`.pio/build/waveshare-prototype/firmware.factory.bin`. The prototype has only
been compiled and host-tested; it has not yet been flashed to a physical
display.

The integrated upstream code remains covered by the MIT licenses in the two
submodules. Preserve both license files when distributing combined binaries.
