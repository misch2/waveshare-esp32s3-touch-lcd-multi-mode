# Multi-mode Waveshare firmware guidance

## Purpose

This repository integrates the pinned `MeteoPlaneRadar` and
`waveshare-hodiny` submodules into one firmware for the Waveshare
ESP32-S3-Touch-LCD-2.1 board.

The desired product is one device with:

- screens from both upstream projects;
- horizontal swipe navigation between screens;
- module-local gestures, such as vertical swipe for radar range;
- one Wi-Fi connection and one web server;
- a common configuration entry point that retains the original module
  configuration features;
- an integration design that permits upstream submodule updates and additional
  screens without repeatedly merging two complete applications.

The goal is integration, not a clean-room rewrite. Reuse working upstream data
clients, parsers, renderers, configuration DTOs, migrations, validation and web
UI wherever practical.

## Current baseline

The integration firmware lives in `firmware/`. The two submodules are upstream
source dependencies, not the application entry points.

Pinned revisions at the time this prototype was created:

- `MeteoPlaneRadar`: `dd77fefd33d6adfa9498a745299e54004cea5694`
- `waveshare-hodiny`: `e1a66810aba21504cf14c239022620e595430f83`

The technical prototype has been compiled, uploaded and smoke-tested on the
physical display. The following are verified:

- ESP32-S3 boots normally;
- the ST7701 panel and LVGL framebuffer work;
- the CST820 touch controller responds (`ChipID 0xb7`, firmware `0x03`);
- the clock dashboard renders;
- the real MeteoPlaneRadar weather renderer loads and displays the CHMI map;
- horizontal screen switching and vertical radar range gestures work and
  remain stable; screens change only by swipe, never on a timer.
- a radar range swipe is acknowledged by a short host-owned LVGL overlay before
  the upstream renderer performs its potentially slow crop/tile work.

- `clock.dashboard` loads the original `ClockConfig` schema and migrations;
- the host reuses the upstream `clock-wifi` NVS and Improv Serial provisioning;
- the host owns SNTP and publishes real Czech local time to the clock dashboard;
- persisted clock appearance/entity settings, Wi-Fi and SNTP have been verified
  on the physical device;
- clock sensor/weather demo values have been removed; the extracted
  `ClockDataService` supplies Home Assistant or Open-Meteo snapshots, and real
  Home Assistant values have been verified on the physical device;
- the original on-device settings overlay opens, saves and affects the clock as
  expected on the physical device;
- the original clock web UI is compiled behind the sole host-owned server on
  port 80. The host landing page is mounted at `/`, the clock page at
  `/clock/`, and its API at `/api/modules/clock/*`;
- the host landing page, `/clock/` page and `/api/modules/clock/*` routes,
  including HTTP access, configuration loading/saving and resulting display
  changes, were verified on the physical device;
- automatic firmware discovery and installation are intentionally disabled in
  the combined build. The web and on-device configuration must not expose or
  enable them. The common host page instead offers a local, manual-only upload
  of the combined `firmware.bin`; physical verification is still required;
- `meteo.radar` now compiles the original `ScreenWeather`, CHMI and RainViewer
  data/rendering path into a host-owned RGB565 canvas which LVGL presents. CHMI
  data, colours, animation, memory headroom and vertical range gestures have
  passed a physical smoke test; RainViewer still requires a separate test;
- `meteo.forecast` compiles the original `Forecast`, `ScreenForecast` and
  `WxIcon` implementations and shares the same host-owned canvas with radar.
  Its Open-Meteo forecast and air-quality requests are serialized with other
  module TLS work and run only while the screen is active; forecast, air
  quality and pollen output have passed a physical smoke test;
- `meteo.planes` compiles the original `ScreenPlanes`, `ADSB` and `Route`
  implementations and shares the host-owned Meteo canvas. It retains the
  upstream map, filtering, persisted range, aircraft detail and cached route
  lookup while serializing its TLS batches through `FetchLease`; rendering,
  navigation and interaction have passed a physical smoke test;
- Home Assistant refreshes reuse one `HTTPClient` and TLS connection for the
  complete entity batch. A transport failure aborts the batch, preserves the
  last complete snapshot and uses a wrap-safe 5--60 second exponential
  backoff instead of multiplying TLS handshakes across every entity;
- the adapted original Meteo page is mounted by the same host at `/meteo/`,
  with configuration, status, remote screen/range control and geocoding below
  `/api/modules/meteo/*`. Configuration access, saving and resulting runtime
  changes have passed a physical smoke test;
- the common landing page exposes authenticated host status and diagnostics,
  plus a versioned combined configuration export/import. The combined backup
  keeps host navigation, clock settings and Meteo settings in separate
  sections and excludes Wi-Fi credentials, HA tokens, web/control passwords
  and every firmware-update field. Download and complete restore have passed a
  physical round-trip test;

Do not add parallel demo implementations for remaining screens. Adapt the
proven upstream functionality instead.

## Non-negotiable architecture

There is exactly one application host and exactly one owner of each physical or
global resource:

- one firmware entry point: `firmware/src/main.cpp`;
- one ST7701 initialization and framebuffer pipeline;
- one LVGL instance and display flush lifecycle;
- one CST820 capture path and central gesture recognizer;
- one TCA9554, backlight and reset implementation;
- one Wi-Fi connection lifecycle;
- one system time/SNTP service;
- one `WebServer` on port 80;
- one screen scheduler.

Never compile both upstream `.ino` entry points, display drivers, touch loops or
global web servers into the combined firmware. They address the same hardware
and cannot safely coexist.

LVGL is the host presentation stack. The existing `waveshare-hodiny` dashboard
is already a native LVGL screen. Meteo screens should be adapted to the host,
preferably by retaining their existing RGB565 rendering logic in an off-screen
surface and presenting that surface through LVGL. Do not introduce another
panel flush owner.

Network callbacks and background tasks must not draw to the display. They may
fetch and parse data, then publish a snapshot or event for the active module to
render from the main/UI context.

All module HTTP clients share `network_host::FetchLease`. A screen/service must
hold the lease only for its active request batch and release it before sleeping
or waiting for the next refresh. This prevents concurrent Home Assistant and
Meteo TLS handshakes from exhausting internal RAM.

Within one Home Assistant refresh, keep one `HTTPClient` alive across all
entity URLs. Arduino-ESP32 3.3.11 stops the underlying client from the
`HTTPClient` destructor, so constructing a new `HTTPClient` per entity defeats
`setReuse(true)` and causes another TLS handshake. Use `setURL()` on the shared
object, abort the remaining batch on a transport result `<= 0`, and retain the
tested `HomeAssistantBatchPolicy` backoff.

## Screen module contract

Every screen implements `ScreenModule` and has a stable string ID. Numeric
screen indexes are runtime details and must not be persisted or exposed as the
configuration identity.

Current IDs:

- `clock.dashboard`
- `meteo.radar`
- `meteo.forecast`
- `meteo.planes`

Expected future IDs include, as applicable:

- other IDs in the owning module's namespace

The compile-time registry in `ScreenManager` is sufficient for this embedded
target. Each module must support the lifecycle:

- `begin()` once after the shared hardware and LVGL host are ready;
- `show()` when becoming active;
- `hide()` before another module becomes active;
- `tick(nowMs)` only for routine UI work;
- `handleGesture(event)` for module-local gestures.

Do not repeatedly initialize upstream global dashboard state in `show()`.
`waveshare-hodiny` currently has global LVGL state, so its dashboard
initialization must happen only once.

Persist screen enabled state and order by stable ID. Unknown syntactically valid
IDs should survive normalization so a configuration remains forward-compatible
with temporarily absent or newly added modules. At least one registered screen
must always remain enabled and reachable.

## Gesture ownership

The central `GestureRecognizer` captures the CST820 stream once and emits tap,
long press, horizontal swipe or vertical swipe.

The contract is:

- horizontal swipe left: next enabled screen;
- horizontal swipe right: previous enabled screen;
- vertical swipe: delivered to the active module;
- radar vertical swipe up/down: increase/decrease radar range;
- tap and long press: delivered to the active module or its LVGL controls;
- an active modal/control may consume a gesture before any optional global
  fallback is applied.

Do not add a second gesture recognizer inside a module. In particular, preserve
the clock dashboard's tap and long-press semantics through LVGL rather than
turning them into global navigation.

LVGL emits the dashboard's `LV_EVENT_SHORT_CLICKED` on pointer release before
the main loop dispatches the completed host gesture. The combined clock adapter
therefore installs `clockDashboardSetShortClickAllowedCallback()` and allows
the manual day/night toggle only while `GestureRecognizer::tapCandidate()` is
true. Keep this narrow filter: it prevents horizontal/vertical swipes from
toggling night mode while preserving a real tap and LVGL long press. Standalone
clock firmware leaves the callback null and retains its original behavior.

## Upstream reuse and adapter policy

Treat both submodules as pinned, read-only upstream sources.

- Do not directly edit files inside either submodule for ordinary integration
  work.
- Do not copy large upstream implementations into `firmware/` and maintain a
  silent fork.
- Prefer thin translation units that include or call pinned upstream sources,
  as used by `firmware/lib/clock_screen` and
  `firmware/lib/upstream_hardware`.
- If upstream code is hidden behind `static` functions or hard-wired globals,
  make the smallest documented adapter patch possible. Keep such patches in
  the integration repository and make their purpose explicit.
- Keep ownership boundaries narrow enough that an upstream update normally
  changes only the relevant adapter.
- Preserve both upstream MIT license files when distributing combined source or
  binaries.

### Clock module

Reuse rather than recreate:

- `ClockDashboard`, fonts, icons, animations and visual settings;
- `ClockConfig`, its current schema, checksum, validation and migration logic;
- Home Assistant request/parsing logic and entity mapping;
- Open-Meteo, weather animation and SNTP/time behavior;
- the existing configuration page, authentication and form validation;
- relevant diagnostics. Firmware update controls remain a standalone-clock
  feature and must be disabled in the combined host.

`ClockConfigCopyStub.cpp` has been removed; `ClockConfigUpstream.cpp` compiles
the real upstream schema, checksum, persistence and migrations.
`FirmwareUpdateServiceStub.cpp` is a deliberate disabled-service compatibility
shim: the reused dashboard references the status API, but the combined build
does not check for or install releases automatically.

`firmware/lib/clock_data_service` is a temporary, provenance-marked extraction
from the pinned `WaveshareHodiny.ino`. It preserves the upstream HTTP clients,
parsers, intervals and FreeRTOS handoff without compiling the second
application entry point. Do not let this become a silent long-lived fork:
upstream the service boundary and replace the extraction with a thin wrapper.

### Meteo module

Reuse rather than recreate:

- aircraft, weather and forecast download clients;
- parsers, filtering, caching and range handling;
- proven renderer behavior and screen-specific interactions;
- `Settings`, its NVS behavior, JSON mapping and validation;
- the existing configuration page and operational endpoints.

`RadarScreen` is now an adapter around the real Meteo radar data and rendering
path. It owns only the host PSRAM canvas, LVGL image bridge and shared fetch-gate
lifecycle; `ScreenWeather`, CHMI/RainViewer downloads, PNG parsing, crop/map
rendering and overlays remain upstream-owned. Do not move those implementations
back into the host module.

`ForecastScreen` reuses the original `Forecast`, `ScreenForecast` and `WxIcon`
sources through thin translation units. Radar, forecast and aircraft share the
one `meteo_canvas` RGB565 PSRAM surface because only the active screen may render.
The forecast adapter holds `network_host::FetchLease` around the complete
upstream fetch/parse batch and uses a host-side data signature to invalidate the
LVGL image when air-quality or pollen values change, which the pinned upstream
screen tick does not observe by itself.

`PlanesScreen` reuses the original `ScreenPlanes`, `ADSB` and `Route` sources
through thin translation units. It shares `meteo_canvas`, holds the host fetch
lease around ADS-B and route work, forwards taps to upstream aircraft
selection and maps vertical swipe to the persisted plane range. The active
module receives a horizontal swipe before global navigation; an open aircraft
detail consumes the first horizontal or vertical swipe and closes, while the
normal horizontal fallback still switches screens. The pinned `Route_Tick()`
has no poll hook and can therefore briefly block the UI while its optional
detail lookup is in progress; preserve
this upstream behavior for the prototype and prefer a small upstream hook or
data-service boundary if it needs improvement.

The combined Arduino-ESP32 3.3.11 build pins Arduino_GFX 1.6.6, because the
upstream standalone pin 1.4.9 predates the framework's changed SPI API. This is
a build-tool compatibility difference, not a fork of the renderer API. PNGdec
stays at 1.0.1; its draw callbacks must retain the declared `void` signature.
CHMI and RainViewer must keep sharing the header-provided decoder: the two
sources are mutually exclusive, and two PNG objects consume about 96 kB of
internal RAM instead of about 48 kB, materially reducing TLS headroom.

## Configuration ownership and persistence

Keep host configuration and module configuration separate.

The host `AppConfig` owns only cross-module concerns such as:

- schema version;
- enabled screens;
- stable screen order;
- future host-level navigation policy.

It is stored as versioned JSON in the `multi-mode` NVS namespace. Its format
must remain small, fixed-bounds and host-testable.

Module settings retain their proven formats and migrations:

- clock settings remain `ClockConfig`, including checksum and migrations, in
  the dedicated `clockcfg` partition / `clock-config` namespace;
- Meteo settings remain the upstream `Settings` model and `planeradar` NVS
  namespace;
- secrets remain subject to the upstream export exclusions and must never be
  added to a combined plaintext export.

Do not flatten all settings into `AppConfig`, translate one upstream DTO into
the other, or discard existing migration logic. A future combined export should
be a versioned envelope containing a host section and separate module sections.

Some concepts exist in both projects, for example location, brightness and
night mode. Do not silently merge fields that currently have different
semantics or ranges. First define an explicit host meaning and documented
mapping; otherwise leave the settings module-local.

`MeteoRadarConfig` is a fixed-size runtime snapshot at the module/service
boundary, not a second persistence format. `firmware/lib/meteo_settings`
compiles the pinned `Settings.cpp` and `Lang.cpp` through thin translation
units. The upstream settings storage callbacks must bracket every runtime
Preferences write with the host display-storage transaction; install them only
after `DisplayHost` is ready, while initial reads/migrations run before RGB
scanout starts.

The combined 16 MB partition table is `firmware/partitions.csv`. Preserve the
standard OTA offsets, the `clockcfg` reservation and the coredump partition when
changing it. Verify the actual generated image offsets after every partition
change.

## Web configuration integration

The final device has one host-owned `WebServer`. Neither module may construct a
second global server on port 80.

`firmware/lib/web_host` owns the only active port-80 `WebServer`, its
`begin()`/`handleClient()` lifecycle and the common landing page. The pinned
clock `ConfigurationWeb.cpp` accepts the caller-owned server through
`ConfigurationWebRoutes` and only registers its page/API handlers. This keeps
the proven clock HTML, form serialization, validation, password/session model,
export/import and diagnostics intact while removing global route ownership from
the module.

The combined host uses `/clock/` and `/api/modules/clock/*` and disables legacy
aliases. The standalone clock entry point keeps `/` and `/api/*` aliases in
addition to the canonical routes, so existing bookmarks and control URLs remain
usable. Do not register Meteo's colliding `/`, `/api/config` or `/api/status`
routes alongside the host routes.

`firmware/lib/meteo_web` mounts the pinned Meteo `PAGE_HTML` without copying it
into a maintained fork. It streams the PROGMEM source in bounded chunks,
rewrites the absolute `/api/*` calls to `/api/modules/meteo/*`, and hides only
the standalone-owned Wi-Fi, clock appearance, auto-rotation, system-password
and OTA controls. Expected source markers are validated when routes register so
an incompatible upstream page update fails visibly. JSON loading and saving
still use the upstream `Settings_ToJson()` / `Settings_FromJson()` model.

Meteo screen checkbox values are translated to the host's stable IDs and saved
in `AppConfig`; the upstream numeric screen mask is not the scheduler source of
truth. Screen and range HTTP commands are queued and applied by the main loop.
The Meteo routes reuse the clock web module's timed-mode, session-cookie and
Origin checks; a locked or protected `/meteo/` page redirects to the shared
clock entry page for activation or login. Geocoding holds the common
`FetchLease`. No Meteo Wi-Fi,
server lifecycle or firmware-update route is registered in the combined build.

Preserve the original user experience and validation logic by adapting route
registration, not by reimplementing the pages from memory. A suitable target
layout is:

- `/` - common landing page, device status and screen order/enabled controls;
- `/clock/` - adapted original clock configuration;
- `/meteo/` - adapted original Meteo configuration;
- `/api/modules/clock/...` - clock API namespace;
- `/api/modules/meteo/...` - Meteo API namespace;
- host-wide status, restart and export/import routes where only one owner is
  meaningful.

Both upstream projects currently use conflicting routes such as `/`,
`/api/config` and `/api/status`. Change the route prefix/base URL in adapters
and the associated existing JavaScript; do not register ambiguous duplicate
routes.

Prefer the stronger clock web access model (timed/always/disabled mode,
password hashing, session cookie and Origin checks) as the host policy. Meteo's
AP/captive-portal provisioning can be retained as the initial Wi-Fi setup path,
with Improv Serial optionally retained as an additional provisioning method.
The precise consolidation must preserve recovery access when credentials are
invalid.

Module web handlers may validate and enqueue changes, but rendering and screen
switches must be applied by the main loop. Keep export/import versioned and omit
Wi-Fi passwords, Home Assistant tokens, admin passwords and control secrets.

The host-wide routes are `/api/status`, `/api/diagnostics`,
`/api/config/export`, `/api/config/import` and the manual-only multipart upload
at `/api/firmware/upload`. They reuse the clock timed-mode, session-cookie and
Origin policy. Import is a replace-style operation: validate the complete
envelope before writing, preserve the stored HA token only when the imported HA
URL is exactly unchanged, preserve access credentials and web mode, and bracket
all namespace writes in one display-storage transaction. Automatic update
policy, discovery state and release metadata are not part of this format or
these diagnostics.

Manual OTA accepts only the combined PlatformIO application image named
`firmware.bin`, never `firmware.factory.bin`. Before writing, the host buffers
and verifies the ESP image header, ESP32-S3 chip ID, application descriptor and
the running combined project's name. The root-owned service then uses
`esp_ota_begin/write/end` so ESP-IDF validates the complete image before the
inactive 6 MiB app partition becomes bootable. It holds `FetchGate` and deletes
the RGB driver for the complete flash transaction; failure or abort restores
the display and releases the gate, while success sends the HTTP result first
and restarts without recreating the old display pipeline.

Every runtime web handler that writes the `clockcfg` partition must use the
host-provided storage begin/end callbacks. The clock configuration and web mode
are persisted inside one nested transaction; password changes use the same
guard. This is required so `DisplayHost` deletes the RGB driver before NVS
temporarily disables the PSRAM cache and recreates it only after all related
writes finish.

## Hardware integration constraints

The two upstream applications target the same board and share pins/resources:

- I2C: SDA 15, SCL 7;
- CST820: address `0x15`, interrupt 16;
- TCA9554: address `0x20`;
- backlight: GPIO 6;
- the same ST7701 RGB and control pins.

They previously used different pixel clocks and different flush semantics. The
combined host must choose and test one panel timing; modules must never adjust
panel timing independently. `DisplayHost` now lowers the clock driver's 14 MHz
pixel clock at runtime to the 8 MHz timing proven by MeteoPlaneRadar.
`DisplayHost` also waits for the RGB-panel VSYNC callback before returning a
full-frame LVGL flush, so LVGL cannot reuse a framebuffer that is still being
scanned out. These changes target transient repeated/shifted horizontal row
blocks observed during full-screen refreshes, especially the 50 ms radar sweep.
They have intentionally not been compiled by Codex at the user's request. The
8 MHz/VSYNC combination has passed a physical display smoke test and eliminated
the transient artifacts during normal redraws.

Configuration saves must call `displayHostRequestFullRedraw()`, never request
an in-stream panel restart. Physical testing showed that even a VSYNC-scheduled
`esp_lcd_rgb_panel_restart()` could leave the whole image in a stable, cyclically
shifted vertical position; another Save/restart merely selected a different
offset or happened to correct it. A Save therefore persists and applies the
configuration, then requests one normal VSYNC-gated LVGL redraw. Do not restore
the upstream immediate restart, its second delayed restart, or a deferred
in-stream restart after Save.

The bundled Arduino 3.3.11 libraries use ESP-IDF 5.5.x with
`CONFIG_LCD_RGB_RESTART_IN_VSYNC=1`, but without PSRAM XIP or an IRAM-safe RGB
ISR. In PSRAM-framebuffer + bounce-buffer mode, NVS/flash writes therefore stop
the ISR from refilling the internal bounce buffers and can desynchronize the
scanout. A physical test proved that `esp_lcd_panel_reset()` plus
`esp_lcd_panel_init()` on the existing handle is not a cold restart and still
leaves the image persistently wrapped: the public reset operation resets the
LCD/FIFO hardware but does not delete the RGB driver object, GDMA descriptors
or callbacks. The storage-write guard must therefore call
`esp_lcd_panel_del()` before NVS, then create a fresh RGB panel with the same
20-line bounce configuration and 8 MHz PCLK, obtain both new framebuffer
addresses, register the VSYNC callback, reset/init the panel and reinitialize
the existing LVGL draw-buffer descriptor. Never let LVGL run while its old
framebuffer pointers refer to the deleted driver. A recreation failure is fatal
because continuing would dereference freed buffers. This lifecycle passed
repeated physical Save testing before the web route refactor. Re-test it through
the new `/clock/` route because password and web-mode writes now use the same
transaction callback.

A no-bounce experiment using direct double-framebuffer PSRAM/EDMA scanout at
8 MHz was physically rejected: the image jittered horizontally on every LVGL
refresh, faster on the radar screen. Do not restore the integration macro
override that sets `ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE` to zero. The current
known-good normal-redraw baseline is the upstream 20-line bounce buffer, 8 MHz
PCLK and the VSYNC flush gate. A proper Save fix must genuinely stop/delete and
recreate the RGB driver pipeline or change the framework configuration; it must
not substitute direct PSRAM scanout without a new measured design. The current
implementation uses the full delete/recreate option only around configuration
storage writes; ordinary screen refreshes retain the upstream bounce pipeline.

Do not remove the VSYNC gate or restore 14 MHz without reproducing the stress
case. A serial `LCD VSYNC timeout during ...` warning means the panel callback
did not arrive within 100 ms and must be investigated rather than silently
bypassed.

The PlatformIO target overrides the generic board profile for 16 MB flash and
OPI PSRAM. Do not infer the physical board capacity solely from the generic
`esp32-s3-devkitc-1` description printed by PlatformIO. Validate PSRAM at runtime
when memory-heavy production renderers are connected.

## Updating a submodule

Update one submodule at a time:

1. Record the old and proposed new commit.
2. Review upstream changes that touch wrapped files, public functions,
   configuration schemas, partitions, display/touch code or web routes.
3. Build/test the upstream project standalone when its documented toolchain is
   available.
4. Advance the submodule pointer without editing unrelated upstream files.
5. Adjust only the relevant integration adapter or documented patch.
6. Run native host tests and the combined firmware build.
7. Flash the physical board and verify display, touch, both swipe directions,
   module-local gestures, networking and web configuration.
8. Record any new compatibility constraint in this file.

Do not update both submodules and the host architecture in one unreviewable
change. Keep combined and standalone build paths usable.

## Validation

Run the dependency-free host tests from the repository root:

```powershell
g++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/lib/app_core/include `
  -Iwaveshare-hodiny/WaveshareHodiny `
  firmware/lib/app_core/src/AppConfig.cpp `
  firmware/lib/app_core/src/GestureRecognizer.cpp `
  firmware/lib/app_core/src/MeteoRadarConfig.cpp `
  firmware/lib/app_core/src/ScreenManager.cpp `
  waveshare-hodiny/WaveshareHodiny/DayNightLogic.cpp `
  firmware/test/native/test_runner.cpp `
  -o firmware/test/native/build/app_core_tests.exe
firmware\test\native\build\app_core_tests.exe
```

Expected output:

```text
All native app-core tests passed
```

Build the hardware firmware with:

```powershell
pio run -d firmware -e waveshare-prototype
```

The validated factory image is written to:

```text
firmware/.pio/build/waveshare-prototype/firmware.factory.bin
```

Also run `git diff --check` and confirm both submodules have a clean worktree.
Compilation alone is not sufficient after display, touch, partition, PSRAM or
network changes. Perform a physical smoke test and state explicitly whether it
was done.

Current host tests cover configuration normalization, the at-least-one-screen
invariant, configured ordering, gesture classification/direction/debounce,
disabled-screen skipping, swipe-only screen navigation and the absence of
timer-driven switching. Extend
these tests whenever the host contracts change. They also cover the reused
day/night transition tolerance, offsets and unavailable-data behavior, plus
the Home Assistant stored-token URL reuse policy used by the web flow. The
route-contract tests also cover the Meteo page/API defaults, disabled legacy
lifecycle/update flags, bounded config/status callbacks, shared access callback
and queued command/storage callback wiring.

## Near-term implementation order

1. Upstream the current provenance-marked `ClockDataService` extraction into
   `waveshare-hodiny`, then replace the extracted source with a thin forwarding
   wrapper like the other upstream adapters.
2. Physically verify the real CHMI and RainViewer radar adapter, including
   RainViewer data, first-load/refresh animation and memory headroom; CHMI and
   the interaction path have already passed.
3. Physically verify the corrected common diagnostics response and the new
   `/clock/` link back to the common landing page, then exercise both a rejected
   wrong image and a successful manual `firmware.bin` upload. Combined
   export/import and its display-safe storage lifecycle have already passed a
   physical round-trip test; its contract leaves Wi-Fi, HA token and web
   credentials untouched.
4. Add regression tests and repeat the hardware smoke test after every phase.

At every phase, prefer a thin wrapper over an imitation of behavior that is
already implemented and tested upstream.
