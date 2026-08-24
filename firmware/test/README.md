# Native app-core tests

`native/test_runner.cpp` is a small dependency-free C++ runner for the host-side
configuration, module-local Meteo radar DTO, clock and Meteo web-route DTOs,
gesture, and screen-navigation contracts. It does not require Arduino, LVGL,
Wi-Fi, or a connected display.

The web-route tests cover the combined Meteo defaults (`/meteo/` and
`/api/modules/meteo`), disabled legacy aliases and lifecycle/update flags,
callback presence helpers (including status/access callbacks), callback pointer
assignment/invocation, and the `MeteoWebOptions` compatibility alias.

From the repository root, build and run it with:

```text
g++ -std=c++17 -Wall -Wextra -Werror ^
  -Ifirmware/lib/app_core/include ^
  -Iwaveshare-hodiny/WaveshareHodiny ^
  firmware/lib/app_core/src/AppConfig.cpp ^
  firmware/lib/app_core/src/GestureRecognizer.cpp ^
  firmware/lib/app_core/src/MeteoRadarConfig.cpp ^
  firmware/lib/app_core/src/ScreenManager.cpp ^
  waveshare-hodiny/WaveshareHodiny/DayNightLogic.cpp ^
  firmware/test/native/test_runner.cpp ^
  -o firmware/test/native/build/app_core_tests.exe
firmware\test\native\build\app_core_tests.exe
```

On a POSIX shell, replace the continuation `^` characters with `\` and use
`./firmware/test/native/build/app_core_tests` as the executable path.

The test source intentionally uses only fixed-size DTOs and the public screen
module interfaces, so it can remain useful while the hardware adapters and
upstream submodules evolve.
