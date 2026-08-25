#!/usr/bin/env python3
"""Validate combined firmware artifacts without flashing hardware."""

from __future__ import annotations

import argparse
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BUILD_DIRECTORY = (
    REPOSITORY_ROOT / "firmware" / ".pio" / "build" / "waveshare-multi-mode"
)
MAX_APPLICATION_SIZE = 6_291_456
COMBINED_IDENTITY_MARKER = b"waveshare-multi-mode-screen:manual-ota:v1"


def nonempty_file(path: Path) -> int:
    if not path.is_file():
        raise SystemExit(f"Required firmware image does not exist: {path}")
    size = path.stat().st_size
    if size == 0:
        raise SystemExit(f"Required firmware image is empty: {path}")
    return size


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIRECTORY,
        help="PlatformIO environment build directory",
    )
    args = parser.parse_args()
    build_directory = args.build_dir
    if not build_directory.is_absolute():
        build_directory = REPOSITORY_ROOT / build_directory
    build_directory = build_directory.resolve()

    application_image = build_directory / "firmware.bin"
    factory_image = build_directory / "firmware.factory.bin"
    application_size = nonempty_file(application_image)
    factory_size = nonempty_file(factory_image)

    if application_size > MAX_APPLICATION_SIZE:
        raise SystemExit(
            f"Application image is {application_size} bytes; "
            f"maximum is {MAX_APPLICATION_SIZE} bytes: {application_image}"
        )
    if COMBINED_IDENTITY_MARKER not in application_image.read_bytes():
        raise SystemExit(
            f"Combined firmware identity marker is missing: {application_image}"
        )

    print(
        "Firmware images are valid: "
        f"application={application_size} bytes, factory={factory_size} bytes"
    )
    print(f"Application image: {application_image}")
    print(f"Factory image: {factory_image}")


if __name__ == "__main__":
    main()
