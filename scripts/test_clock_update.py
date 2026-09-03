#!/usr/bin/env python3
"""Focused clock backup and upstream TMEP tests; run after PlatformIO setup."""

import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "firmware/test/native/build"


def main():
    json_headers = ROOT / "firmware/.pio/libdeps/waveshare-multi-mode/ArduinoJson/src"
    if not (json_headers / "ArduinoJson.h").is_file():
        raise SystemExit("Run pio run -d firmware first to install the pinned ArduinoJson headers.")
    BUILD.mkdir(parents=True, exist_ok=True)
    includes = [
        ROOT / "firmware/test/native/include",
        ROOT / "firmware/lib/app_core/include",
        ROOT / "firmware/lib/combined_config/include",
        ROOT / "waveshare-hodiny/WaveshareHodiny",
        json_headers,
    ]
    cases = {
        "combined_config_tests": [
            "firmware/test/native/test_combined_config_codec.cpp",
            "firmware/lib/combined_config/src/CombinedConfigCodec.cpp",
            "firmware/lib/app_core/src/AppConfig.cpp",
            "waveshare-hodiny/WaveshareHodiny/ClockConfig.cpp",
        ],
        "tmep_parser_tests": [
            "waveshare-hodiny/tools/test_tmep_parser.cpp",
            "waveshare-hodiny/WaveshareHodiny/TmepParser.cpp",
        ],
    }
    for name, sources in cases.items():
        binary = BUILD / (name + (".exe" if os.name == "nt" else ""))
        command = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra"]
        command += [f"-I{path}" for path in includes]
        command += [str(ROOT / path) for path in sources]
        subprocess.run(command + ["-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)
        print(f"{name}: passed", flush=True)


if __name__ == "__main__":
    main()
