# Multi-mode Waveshare firmware

Release versions come from exact annotated `vMAJOR.MINOR.PATCH` Git tags. An
exact `v1.1.0` tag therefore builds firmware reporting `1.1.0`; untagged builds
report a development version containing the commit hash and optional dirty
state.

This is the combined firmware for the two pinned upstream submodules. Small
integration seams are kept explicit in those pinned source trees. The firmware
has one LVGL/display/touch owner, a compile-time screen registry, stable screen
IDs and a central gesture recognizer.

## Upstream provenance and update workflow

`UPSTREAMS.json` in the repository root is the source of truth for each
submodule's exact fork pin, incorporated upstream base, optional exact release
tag and repository URLs. The present bases are exactly tagged upstream as
`waveshare-hodiny` `v1.5.5` and MeteoPlaneRadar `v0.6.3`. The generated
`BuildProvenance.h` exposes the same build-time provenance through the host
diagnostics endpoint and landing page; the commit remains the immutable
identity.

Update one submodule at a time with the two dependency-free Python commands
below. They read the repository URLs, branches and recorded pins from
`UPSTREAMS.json`, reject dirty or divergent state, and never push or commit.

First prepare an explicit merge on a local review branch (use
`meteo-plane-radar` or `waveshare-hodiny`):

```powershell
python scripts/prepare_upstream_update.py meteo-plane-radar
```

The command verifies or adds the expected `fork` and `upstream` remotes,
fetches them, fast-forwards local `main` exactly to `fork/main`, prints the
fork/upstream divergence, creates a dated `sync/...` branch and performs the
non-fast-forward upstream merge. It highlights changed configuration, public
headers, display/touch, screen and web paths for adapter review. If conflicts
occur, the merge is deliberately left in progress for manual resolution.
`--dry-run` prints the planned mutations without fetching or changing refs.

Review the diff and the integration adapters, resolve and commit any conflicts,
run the upstream project's standalone validation, then push the review branch
and merge its PR into the fork. Pushing and PR creation stay manual so the
review gate cannot be bypassed by the helper.

After the fork PR has merged, finalize the combined-repository pin:

```powershell
python scripts/finalize_upstream_pin.py meteo-plane-radar
python scripts/test_combined_firmware.py
```

The finalize command fetches both remotes, accepts only a clean fork tip that
local `main` can fast-forward to exactly, calculates the incorporated upstream
base, updates only the selected `UPSTREAMS.json` entry, regenerates
`BuildProvenance.h`, stages the manifest/header/gitlink and validates all staged
provenance. It does not commit. The combined validation covers native tests,
staged provenance, the PlatformIO build and image/worktree checks; the required
physical display, touch, gesture, network and web smoke test remains a separate
manual gate and must be recorded explicitly.

Do not use `git submodule update --remote`, which follows the fork URL without
the review step, and do not rebase published `fork/main`, because released
combined-firmware commits must retain valid historic gitlinks. After changing
a pin, update `UPSTREAMS.json` and run from the repository root:

```powershell
python scripts/test_upstream_provenance.py
```

`scripts/Test-UpstreamProvenance.ps1` remains as a compatibility wrapper.

## Configuration and functionality

Current gesture contract:

- horizontal swipe: previous/next screen;
- screens never change automatically on a timer;
- vertical swipe: delivered to the active module (weather and aircraft radar
  change their range);
- tap and long press: delivered to the active module/LVGL controls.

Both radar screens acknowledge a vertical range swipe immediately with a short
LVGL overlay containing the direction and newly selected range. It is flushed
before the upstream renderer starts its potentially slow data work, so a
registered gesture does not look ignored.

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
  common fetch lease. Forecast, air quality and pollen output have passed a
  physical smoke test.
- `meteo.planes` wraps the pinned MeteoPlaneRadar `ScreenPlanes`, `ADSB` and
  `Route` implementations. It retains the original adsb.fi parser, map,
  filters, range persistence, tap-to-select detail and cached adsbdb route
  lookup. ADS-B and route TLS work is serialized through the common fetch
  lease; the adapter reuses the same Meteo canvas. Rendering, navigation and
  interaction have passed a physical smoke test.

The host configuration is stored as versioned JSON in NVS. Schema 3 uses stable
screen IDs, enabled flags and configured order, always keeps at least one
screen reachable, appends newly introduced built-in screens without disturbing
an existing order, and deliberately omits timed rotation.
Clock settings retain the original binary schema, checksum,
migrations and dedicated `clockcfg` partition. The combined partition table
retains two application slots for safe manual firmware deployment, but the
running firmware does not contact a release server or expose an automatic
update action.

Build from this directory with `pio run`. `platformio.ini` pins PIOArduino
55.03.311 (Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5), so a clean CI runner uses the
same framework as the physically verified build. The first build may need to
download the platform and LVGL 8.3.10. This is the standard combined firmware
for the Waveshare board. The two upstream projects remain standalone source
dependencies and standalone builds.

Validated output is written to
`.pio/build/waveshare-multi-mode/firmware.factory.bin`. The display and gesture
paths, persisted clock settings, Wi-Fi and SNTP were hardware-
tested. Real Home Assistant values and the on-device settings overlay were also
verified. The host landing page, prefixed clock routes, HTTP configuration
persistence and resulting display updates were verified on the physical device
as well.

The clock web module now accepts the host-owned `WebServer`. The combined
firmware mounts it at `/clock/` and `/api/modules/clock/*`; the standalone clock
firmware retains its original `/` and `/api/*` aliases. The original HTML,
serialization, validation, password/session handling and export/import logic
remain upstream.

The same server now mounts the adapted original Meteo page at `/meteo/` and
its configuration, status, remote screen/range and geocoding endpoints at
`/api/modules/meteo/*`. The adapter streams the pinned PROGMEM page rather than
maintaining a copied UI, rewrites its absolute API paths and hides controls
owned by the combined host (Wi-Fi, clock appearance, automatic rotation,
system password and OTA). Meteo JSON still comes from the original
`Settings_ToJson()` / `Settings_FromJson()` implementation. Screen checkboxes
map to stable `AppConfig` IDs, commands are executed later by the UI loop, and
all routes reuse the clock web access/session/Origin policy. A locked or
protected Meteo page redirects to the shared clock entry page for activation
or login. Firmware updates remain manual-only.

The common landing page also exposes host-level status and diagnostics plus a
combined backup/restore flow. The canonical routes are `/api/status`,
`/api/diagnostics`, `/api/config/export` and `/api/config/import`. The backup is
a versioned envelope with independent host, clock and Meteo sections. It never
contains Wi-Fi credentials, the Home Assistant token, web/control passwords or
firmware-update fields; web access mode and credentials are preserved locally
during a restore. The complete payload is validated before the first NVS write,
and all writes share one display-safe storage transaction.

Firmware release discovery and installation from a remote download are
intentionally absent. Manual deployment is available on the common page through `POST
/api/firmware/upload`: choose the application image
`.pio/build/waveshare-multi-mode/firmware.bin` (the standard build artifact may
be renamed to any filename ending in `.bin`). The filename is not used as an
identity check: the host validates the ESP32-S3 application header and combined
identity marker, so `firmware.factory.bin` and other non-combined images are
rejected by their contents. It then stops the RGB driver and competing network
fetches for the flash transaction, uses ESP-IDF OTA validation, and restarts
only after returning a successful upload response. The next boot confirms the
new image to the rollback-enabled bootloader after setup starts successfully.

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

Radar, forecast and aircraft share one host-owned `meteo_canvas` PSRAM
allocation and select its presentation callback from their `show()`/`hide()`
lifecycle. This keeps the upstream global `gfx` contract intact without
allocating another roughly 460 kB surface per screen or introducing another
panel flush owner.

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
