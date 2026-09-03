#!/usr/bin/env python3
"""Run the complete non-hardware validation for the combined firmware."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
SCRIPTS_DIRECTORY = REPOSITORY_ROOT / "scripts"
FACTORY_IMAGE = (
    REPOSITORY_ROOT
    / "firmware"
    / ".pio"
    / "build"
    / "waveshare-multi-mode"
    / "firmware.factory.bin"
)


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=REPOSITORY_ROOT, check=True)


def verify_clean_submodules() -> None:
    manifest = json.loads(
        (REPOSITORY_ROOT / "UPSTREAMS.json").read_text(encoding="utf-8")
    )
    for component in manifest.get("components", []):
        component_id = component.get("id", "unknown")
        relative_path = component.get("path")
        if not isinstance(relative_path, str) or not relative_path:
            raise SystemExit(f"{component_id}: UPSTREAMS.json has no valid path")
        submodule_path = REPOSITORY_ROOT / relative_path
        if not (submodule_path / ".git").exists():
            raise SystemExit(f"{component_id}: submodule is not initialized")
        result = subprocess.run(
            ["git", "-C", str(submodule_path), "status", "--porcelain"],
            cwd=REPOSITORY_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout.strip():
            raise SystemExit(
                f"{component_id}: submodule worktree is not clean:\n{result.stdout.rstrip()}"
            )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-native", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    if not args.skip_native:
        run([sys.executable, str(SCRIPTS_DIRECTORY / "test_native_app_core.py")])
    run([sys.executable, str(SCRIPTS_DIRECTORY / "test_git_version.py")])
    run([sys.executable, str(SCRIPTS_DIRECTORY / "test_upstream_tag_contract.py")])
    run([sys.executable, str(SCRIPTS_DIRECTORY / "test_upstream_provenance.py")])
    if not args.skip_build:
        run(
            [
                os.environ.get("PIO", "pio"),
                "run",
                "-d",
                "firmware",
                "-e",
                "waveshare-multi-mode",
            ]
        )

    run([sys.executable, str(SCRIPTS_DIRECTORY / "test_clock_update.py")])
    run([sys.executable, str(SCRIPTS_DIRECTORY / "verify_firmware_images.py")])
    run(["git", "diff", "--check"])
    verify_clean_submodules()

    print("Combined firmware validation passed.")
    print(f"Factory image: {FACTORY_IMAGE}")
    print("Physical hardware validation was not performed.")


if __name__ == "__main__":
    main()
