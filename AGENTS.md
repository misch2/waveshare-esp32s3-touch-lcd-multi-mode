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

- `MeteoPlaneRadar`: `792ef8d05b0900a81e0f49697b8e72220a89f4a7`
- `waveshare-hodiny`: `9537a76932fc9269b2a22a5fb90a62785897c680`

The technical prototype has been compiled, uploaded and smoke-tested on the
physical display. The following are verified:

- ESP32-S3 boots normally;
- the ST7701 panel and LVGL framebuffer work;
- the CST820 touch controller responds (`ChipID 0xb7`, firmware `0x03`);
- the clock dashboard renders;
- the radar demonstrator renders;
- horizontal screen switching, vertical radar range gestures and automatic
  rotation work and remain stable.

This is still a prototype:

- `clock.dashboard` uses deterministic demo sensor/weather values and an
  uptime-based clock;
- `meteo.radar` is a visual/gesture demonstrator and does not use real
  MeteoPlaneRadar network data;
- the common web configuration, Wi-Fi, SNTP, Home Assistant and production OTA
  flows are not connected yet.

Do not extend the demo values or demo radar into parallel production
implementations. Replace them by adapting the proven upstream functionality.

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

## Screen module contract

Every screen implements `ScreenModule` and has a stable string ID. Numeric
screen indexes are runtime details and must not be persisted or exposed as the
configuration identity.

Current IDs:

- `clock.dashboard`
- `meteo.radar`

Expected future IDs include, as applicable:

- `meteo.planes`
- `meteo.forecast`
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
- relevant diagnostics and firmware-update behavior after they are moved under
  the single host server.

The current `FirmwareUpdateServiceStub.cpp` and `ClockConfigCopyStub.cpp` are
prototype link shims, not intended production replacements. Remove each stub
when the real owning upstream service is adapted.

### Meteo module

Reuse rather than recreate:

- aircraft, weather and forecast download clients;
- parsers, filtering, caching and range handling;
- proven renderer behavior and screen-specific interactions;
- `Settings`, its NVS behavior, JSON mapping and validation;
- the existing configuration page and operational endpoints.

The current `RadarScreen` is only a host/gesture demonstrator. It must eventually
be replaced by an adapter around the real Meteo radar data and rendering path,
not expanded into an independent clone.

## Configuration ownership and persistence

Keep host configuration and module configuration separate.

The host `AppConfig` owns only cross-module concerns such as:

- schema version;
- enabled screens;
- stable screen order;
- automatic rotation interval;
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

The combined 16 MB partition table is `firmware/partitions.csv`. Preserve the
standard OTA offsets, the `clockcfg` reservation and the coredump partition when
changing it. Verify the actual generated image offsets after every partition
change.

## Web configuration integration

The final device has one host-owned `WebServer`. Neither module may construct a
second global server on port 80.

Preserve the original user experience and validation logic by adapting route
registration, not by reimplementing the pages from memory. A suitable target
layout is:

- `/` - common landing page, device status and screen order/enabled controls;
- `/clock/` - adapted original clock configuration;
- `/meteo/` - adapted original Meteo configuration;
- `/api/modules/clock/...` - clock API namespace;
- `/api/modules/meteo/...` - Meteo API namespace;
- host-wide status, restart, export/import and OTA routes where only one owner
  is meaningful.

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

## Hardware integration constraints

The two upstream applications target the same board and share pins/resources:

- I2C: SDA 15, SCL 7;
- CST820: address `0x15`, interrupt 16;
- TCA9554: address `0x20`;
- backlight: GPIO 6;
- the same ST7701 RGB and control pins.

They previously used different pixel clocks and different flush semantics. The
combined host must choose and test one panel timing; modules must never adjust
panel timing independently. Current builds use the hardware wrapper selected by
the prototype, but any timing change requires a physical display smoke test.

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
   module-local gestures, automatic rotation, networking and web configuration.
8. Record any new compatibility constraint in this file.

Do not update both submodules and the host architecture in one unreviewable
change. Keep combined and standalone build paths usable.

## Validation

Run the dependency-free host tests from the repository root:

```powershell
g++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/lib/app_core/include `
  firmware/lib/app_core/src/AppConfig.cpp `
  firmware/lib/app_core/src/GestureRecognizer.cpp `
  firmware/lib/app_core/src/ScreenManager.cpp `
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
disabled-screen skipping, screen navigation and automatic rotation. Extend
these tests whenever the host contracts change.

## Near-term implementation order

1. Replace clock demo data with adapters around the existing `ClockConfig`,
   Wi-Fi/SNTP, Home Assistant and Open-Meteo services.
2. Replace the radar demonstrator with the real Meteo data/cache/rendering
   adapter while retaining vertical range gestures.
3. Add the remaining Meteo screens as separate stable-ID modules.
4. Introduce the single host web server and mount the adapted original clock
   and Meteo configuration pages under module prefixes.
5. Add combined configuration export/import, diagnostics and host-owned OTA.
6. Add regression tests and repeat the hardware smoke test after every phase.

At every phase, prefer a thin wrapper over an imitation of behavior that is
already implemented and tested upstream.
