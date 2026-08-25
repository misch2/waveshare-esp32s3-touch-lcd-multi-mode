#!/usr/bin/env python3
"""Validate staged submodule pins against UPSTREAMS.json."""

from __future__ import annotations

import argparse
import sys

from upstream_update_common import (
    UpstreamUpdateError,
    component_path,
    git,
    git_output,
    load_manifest,
    staged_gitlink,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-ancestry", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest = load_manifest()
        for component in manifest["components"]:
            path = component_path(component)
            staged_pin = staged_gitlink(component)
            if staged_pin != component["forkPin"]:
                raise UpstreamUpdateError(
                    f"{component['id']}: manifest forkPin does not match staged gitlink "
                    f"(expected {staged_pin})."
                )

            checked_out = git_output(path, "rev-parse", "HEAD")
            if checked_out != component["forkPin"]:
                raise UpstreamUpdateError(
                    f"{component['id']}: checked-out submodule does not match its pinned gitlink."
                )

            if not args.skip_ancestry:
                if git(path, "cat-file", "-e", f"{component['upstreamBase']}^{{commit}}", check=False).returncode != 0:
                    raise UpstreamUpdateError(
                        f"{component['id']}: upstreamBase object is unavailable."
                    )
                if git(
                    path,
                    "merge-base",
                    "--is-ancestor",
                    component["upstreamBase"],
                    component["forkPin"],
                    check=False,
                ).returncode != 0:
                    raise UpstreamUpdateError(
                        f"{component['id']}: upstreamBase is not an ancestor of forkPin."
                    )
        print("Upstream provenance is valid")
        return 0
    except (KeyError, UpstreamUpdateError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
