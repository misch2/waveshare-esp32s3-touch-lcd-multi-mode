# Changelog

All notable changes to this firmware are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed

- Restored MeteoPlaneRadar's first-boot GeoIP location fallback when no manual
  location is stored, serialized through the combined host's network and
  display-safe storage lifecycles.

## [1.1.0] - 2026-08-26

### Added

- A host-owned navigation indicator restores the original Meteo screen dots
  across radar, forecast and aircraft views while following the configured
  stable-ID order.
- The combined firmware version is derived from an exact annotated
  `vMAJOR.MINOR.PATCH` Git tag. Untagged builds identify themselves with their
  commit hash and dirty-worktree state instead of claiming a release version.

### Fixed

- Meteo radar and aircraft screens now continue refreshing the shared outside
  temperature instead of leaving the status value stale.
- The clock settings overlay now displays the combined firmware version.

## [1.0.3] - 2026-08-25

### Changed

- Component versions on the common web page now show an exact recorded
  upstream release tag when one points at the incorporated upstream commit,
  while retaining the immutable commit link.

## [1.0.2] - 2026-08-25

### Fixed

- Restored the clock dashboard's upstream animated weather icons in the
  combined firmware.

## [1.0.1] - 2026-08-25

### Added

- Recorded exact upstream and fork provenance and added guarded helpers for
  reviewing and advancing pinned upstream revisions.

## [1.0.0] - 2026-08-24

### Added

- Combined Waveshare ESP32-S3 firmware with one LVGL display pipeline, one
  touch/gesture path, one Wi-Fi lifecycle and one web server.
- Horizontal swipe navigation between the clock, radar, forecast and aircraft
  screens. Vertical gestures remain module-local, including radar range
  selection and immediate range feedback.
- Reused `waveshare-hodiny` clock dashboard with its ClockConfig migrations,
  Home Assistant and Open-Meteo data paths, SNTP time and on-device settings.
- Reused MeteoPlaneRadar weather, forecast and aircraft renderers, parsers,
  settings and network clients through host adapters.
- Common web entry point with clock and Meteo configuration pages, diagnostics,
  versioned configuration export/import, and access/session protection.
- Manual firmware upload from the common page. The upload validates the
  ESP32-S3 application image and combined-firmware identity marker, pauses the
  display-safe storage lifecycle during flashing and confirms the image for
  rollback-enabled boot.

### Changed

- Component provenance is recorded as exact fork pins and incorporated
  upstream bases: MeteoPlaneRadar `dd77fef` / `792ef8d`, and
  waveshare-hodiny `e1a6681` / `9537a769`. Device diagnostics expose the
  same values.
- Stable screen IDs and configured ordering are persisted in host NVS; timed
  screen rotation is disabled and screen changes are swipe-only.
- Clock and Meteo routes are namespaced under `/clock/` and `/meteo/` while
  preserving the upstream configuration behavior.
- The display host uses the tested 8 MHz RGB pixel clock, VSYNC-gated flushes
  and a full RGB driver delete/recreate around flash/NVS writes.
- Home Assistant refreshes reuse one HTTP/TLS client per entity batch and use
  bounded backoff after transport failures to reduce internal-heap pressure.
- The combined build uses the pinned PIOArduino 55.03.311 toolchain,
  Arduino_GFX 1.6.6, PNGdec 1.0.1 and the 16 MB/8 MB board configuration.

### Removed

- Automatic firmware discovery, release download and installation. Updates
  are manual only.
- Upstream standalone Wi-Fi, display, touch, web-server and application-entry
  point ownership from the combined build.

### Fixed

- Transient repeated or vertically shifted framebuffer rows during normal
  full-screen refreshes and configuration saves.
- Clock night-mode toggling caused by releasing a horizontal or vertical swipe
  on the dashboard.
- Concurrent TLS handshakes between Home Assistant and Meteo data clients.
