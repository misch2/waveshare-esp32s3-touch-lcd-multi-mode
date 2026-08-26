#!/usr/bin/env python3
"""Build and run the dependency-free native app-core test suite."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIRECTORY = REPOSITORY_ROOT / "firmware" / "test" / "native" / "build"
TEST_BINARY = BUILD_DIRECTORY / (
    "app_core_tests.exe" if os.name == "nt" else "app_core_tests"
)


def main() -> None:
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
