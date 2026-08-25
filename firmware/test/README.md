# Native app-core tests

`native/test_runner.cpp` is a small dependency-free C++ runner for the host-side
configuration, combined export/import and diagnostics route DTO, module-local
Meteo radar DTO, clock and Meteo web-route DTOs, gesture, and screen-navigation
contracts. It does not require Arduino, LVGL, Wi-Fi, or a connected display.

The combined web-route tests cover the canonical status, diagnostics, export
and import paths, the bounded 16 KiB import limit, default null callbacks and
helper results, callback assignment and invocation (including the import detail
buffer, validation/apply phases and storage callbacks), and the `CombinedWebOptions` compatibility
alias. Manual-only firmware upload is part of the route contract: its canonical
path, 6 MiB bound, complete lifecycle callbacks and chunk buffers are tested.
The upload filename is deliberately not the image identity; a release may use
any filename ending in `.bin`. The hardware OTA service validates the
ESP32-S3 application header and combined identity marker, so factory images and
other non-combined applications are rejected by their contents.
Automatic update discovery and installation remain intentionally absent from
this contract.

The web-route tests cover the combined Meteo defaults (`/meteo/` and
`/api/modules/meteo`), disabled legacy aliases and lifecycle/update flags,
callback presence helpers (including status/access callbacks), callback pointer
assignment/invocation, and the `MeteoWebOptions` compatibility alias.

From the repository root, build and run it with the cross-platform script:

```text
python scripts/test_native_app_core.py
```

The script resolves paths from its own location, creates the native build
directory when needed, stops on compilation or test failure, and runs the
resulting platform-specific executable. Set `CXX` to use a compiler other than
`g++`. The same script is used by the Linux GitHub runners.

The test source intentionally uses only fixed-size DTOs and the public screen
module interfaces, so it can remain useful while the hardware adapters and
upstream submodules evolve.
