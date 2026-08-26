#!/usr/bin/env python3
"""Build and run the dependency-free native app-core test suite."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIRECTORY = REPOSITORY_ROOT / "firmware" / "test" / "native" / "build"
TEST_BINARY = BUILD_DIRECTORY / (
    "app_core_tests.exe" if os.name == "nt" else "app_core_tests"
)


def _read_text(path: Path) -> str:
    # A few upstream-derived headers retain a Windows code-page copyright
    # character.  Source-contract checks only need searchable text, so do not
    # make the native test depend on every file being valid UTF-8.
    return path.read_text(encoding="utf-8", errors="replace")


def _firmware_sources() -> list[Path]:
    """Return integration-owned C/C++ sources, excluding generated tests."""

    return [
        path
        for path in (REPOSITORY_ROOT / "firmware").rglob("*")
        if path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"}
        and ".pio" not in path.parts
        and "test" not in path.parts
        and "build" not in path.parts
    ]


def test_geoip_integration_contract() -> None:
    """Check the host-owned seams around the pinned GeoIP implementation.

    GeoIP itself depends on Arduino, Wi-Fi and HTTPClient, so linking it into
    the dependency-free native runner would make this test less portable and
    would not exercise the hardware path.  Instead, keep the integration
    contract explicit here: the upstream translation unit must be compiled,
    every host invocation must hold the common network lease, and the host
    must install the storage guard before the detector can write location.
    """

    upstream_geoip = (
        REPOSITORY_ROOT / "MeteoPlaneRadar" / "MeteoPlaneRadar" / "GeoIP.cpp"
    )
    upstream_settings = (
        REPOSITORY_ROOT / "MeteoPlaneRadar" / "MeteoPlaneRadar" / "Settings.cpp"
    )
    assert upstream_geoip.is_file(), "pinned upstream GeoIP.cpp is missing"
    assert upstream_settings.is_file(), "pinned upstream Settings.cpp is missing"

    source_text = {
        path: _read_text(path) for path in _firmware_sources()
    }

    # The implementation must remain a thin translation unit over the pinned
    # source.  Copying the detector into the host would silently fork its
    # parser, endpoint and location sanity checks.
    geoip_translation_units = [
        (path, text)
        for path, text in source_text.items()
        if "GeoIP.cpp" in text
        or "GeoIP_DetectIfNeeded" in text
    ]
    assert geoip_translation_units, (
        "combined firmware does not compile or adapt the upstream GeoIP detector"
    )

    detector_sources = [
        (path, text)
        for path, text in geoip_translation_units
        if "GeoIP_DetectIfNeeded" in text
    ]
    assert detector_sources, "GeoIP detector is not reachable from host code"

    # Do not permit a direct HTTPClient batch to compete with Home Assistant
    # or Meteo.  The lease must be held in the same adapter/host unit that
    # calls the detector, rather than merely existing somewhere in the build.
    lease_callers = [
        (path, text)
        for path, text in detector_sources
        if re.search(r"\bFetchLease\b", text)
    ]
    assert lease_callers, (
        "GeoIP_DetectIfNeeded must be called while holding network_host::FetchLease"
    )

    # Settings_SetLocation() writes planeradar NVS.  The combined host must
    # install the existing display-safe callbacks before any delayed first-boot
    # detector invocation; otherwise the PSRAM RGB pipeline can be interrupted
    # by flash access.
    settings_adapter = source_text.get(
        REPOSITORY_ROOT / "firmware" / "lib" / "meteo_settings" / "src"
        / "MeteoSettingsAdapter.cpp",
        "",
    )
    upstream_settings_adapter = source_text.get(
        REPOSITORY_ROOT / "firmware" / "lib" / "meteo_settings" / "src"
        / "MeteoSettingsUpstream.cpp",
        "",
    )
    host_source = source_text.get(REPOSITORY_ROOT / "firmware" / "src" / "main.cpp", "")
    assert "Settings_SetStorageCallbacks" in settings_adapter
    assert "Settings.cpp" in upstream_settings_adapter
    assert "meteo_settings::setStorageCallbacks" in host_source
    assert "beginConfigurationStorageWrite" in host_source
    assert "endConfigurationStorageWrite" in host_source
    assert "applyFirstBootGeoIp();" in host_source
    assert "geoIpAttemptedWhileConnected" in host_source
    assert "GeoIpAttemptResult::Busy" in host_source
    assert "GeoIpAttemptResult::Detected" in host_source
    assert "meteoRestartPending = true" in host_source

    # Preserve the upstream first-boot guard: a configured/manual location is
    # never replaced by a GeoIP result.
    upstream_geoip_text = _read_text(upstream_geoip)
    assert "Settings_HasLocation()" in upstream_geoip_text
    assert "if (Settings_HasLocation()) return false;" in upstream_geoip_text


def main() -> None:
    print("Checking GeoIP integration contract...", flush=True)
    test_geoip_integration_contract()
    BUILD_DIRECTORY.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get("CXX", "g++")
    compile_command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{REPOSITORY_ROOT / 'firmware/test/native/include'}",
        f"-I{REPOSITORY_ROOT / 'firmware/lib/app_core/include'}",
        f"-I{REPOSITORY_ROOT / 'firmware/lib/navigation_indicator/include'}",
        f"-I{REPOSITORY_ROOT / 'waveshare-hodiny/WaveshareHodiny'}",
        str(REPOSITORY_ROOT / "firmware/lib/app_core/src/AppConfig.cpp"),
        str(
            REPOSITORY_ROOT
            / "firmware/lib/app_core/src/ClockWeatherAnimationPolicy.cpp"
        ),
        str(REPOSITORY_ROOT / "firmware/lib/app_core/src/GestureRecognizer.cpp"),
        str(REPOSITORY_ROOT / "firmware/lib/app_core/src/MeteoRadarConfig.cpp"),
        str(
            REPOSITORY_ROOT
            / "firmware/lib/app_core/src/NavigationIndicatorModel.cpp"
        ),
        str(
            REPOSITORY_ROOT
            / "firmware/lib/navigation_indicator/src/NavigationIndicator.cpp"
        ),
        str(REPOSITORY_ROOT / "firmware/lib/app_core/src/ScreenManager.cpp"),
        str(REPOSITORY_ROOT / "waveshare-hodiny/WaveshareHodiny/DayNightLogic.cpp"),
        str(REPOSITORY_ROOT / "firmware/test/native/test_runner.cpp"),
        "-o",
        str(TEST_BINARY),
    ]

    print("Building native app-core tests...", flush=True)
    subprocess.run(compile_command, cwd=REPOSITORY_ROOT, check=True)
    print("Running native app-core tests...", flush=True)
    subprocess.run([str(TEST_BINARY)], cwd=REPOSITORY_ROOT, check=True)


if __name__ == "__main__":
    main()
