#!/usr/bin/env python3
"""Prepare one explicit upstream-to-fork review merge."""

from __future__ import annotations

import argparse
import sys
from datetime import date

from upstream_update_common import (
    UpstreamUpdateError,
    component_path,
    ensure_remote,
    full_commit,
    git,
    git_output,
    load_manifest,
    require_clean_submodule,
    require_recorded_parent_pin,
    select_component,
)


RISK_MARKERS = (
    ".ino",
    ".h",
    "config",
    "setting",
    "partition",
    "display",
    "screen",
    "touch",
    "web",
    "wifi",
    "http",
    "lvgl",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch and merge one manifest-defined upstream into a local fork review branch."
    )
    parser.add_argument("component", help="Component id, path, or display name from UPSTREAMS.json")
    parser.add_argument("--fork-ref", default="main", help="Fork branch to update from (default: main)")
    parser.add_argument("--branch", help="Local review branch name")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate local state and show the mutating commands without running them",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest = load_manifest()
        component = select_component(manifest, args.component)
        path = component_path(component)
        require_clean_submodule(path)
        require_recorded_parent_pin(component, path, require_head=True)
        ensure_remote(path, "fork", component["forkUrl"], dry_run=args.dry_run)
        ensure_remote(path, "upstream", component["upstreamUrl"], dry_run=args.dry_run)

        branch = args.branch or f"sync/{component['id']}-upstream-{date.today().isoformat()}"
        upstream_remote_ref = f"upstream/{component['upstreamRef']}"
        fork_remote_ref = f"fork/{args.fork_ref}"

        print(f"Component: {component['displayName']} ({component['path']})")
        print(f"Recorded fork pin: {component['forkPin']}")
        print(f"Recorded upstream base: {component['upstreamBase']}")

        if args.dry_run:
            commands = (
                f"git -C {component['path']} fetch --prune fork",
                f"git -C {component['path']} fetch --prune upstream",
                f"git -C {component['path']} switch main",
                f"git -C {component['path']} merge --ff-only {fork_remote_ref}",
                f"git -C {component['path']} switch -c {branch}",
                f"git -C {component['path']} merge --no-ff --no-edit {upstream_remote_ref}",
            )
            print("\nPlanned commands:")
            print("\n".join(f"  {command}" for command in commands))
            return 0

        if git(path, "show-ref", "--verify", "--quiet", f"refs/heads/{branch}", check=False).returncode == 0:
            raise UpstreamUpdateError(f"Local branch '{branch}' already exists; pass --branch with a new name.")
        if git(path, "show-ref", "--verify", "--quiet", f"refs/remotes/fork/{branch}", check=False).returncode == 0:
            raise UpstreamUpdateError(f"Fork branch '{branch}' already exists; pass --branch with a new name.")

        git(path, "fetch", "--prune", "fork")
        git(path, "fetch", "--prune", "upstream")
        if git(path, "show-ref", "--verify", "--quiet", f"refs/remotes/fork/{branch}", check=False).returncode == 0:
            raise UpstreamUpdateError(f"Fork branch '{branch}' already exists; pass --branch with a new name.")
        fork_tip = full_commit(path, fork_remote_ref)
        upstream_tip = full_commit(path, upstream_remote_ref)
        if git(path, "merge-base", "--is-ancestor", component["forkPin"], fork_tip, check=False).returncode != 0:
            raise UpstreamUpdateError("Fetched fork tip does not descend from the recorded release pin.")
        if git(path, "merge-base", "--is-ancestor", upstream_tip, fork_tip, check=False).returncode == 0:
            raise UpstreamUpdateError("The selected upstream tip is already contained in the fork tip.")

        if git(path, "show-ref", "--verify", "--quiet", "refs/heads/main", check=False).returncode == 0:
            git(path, "switch", "main")
        else:
            git(path, "switch", "-c", "main", "--track", fork_remote_ref)
        git(path, "merge", "--ff-only", fork_remote_ref)
        if git_output(path, "rev-parse", "HEAD") != fork_tip:
            raise UpstreamUpdateError(
                f"Local main is not exactly {fork_remote_ref}. Resolve the divergence explicitly."
            )
        print(f"\nFork tip: {fork_tip}")
        print(f"Upstream tip: {upstream_tip}")
        print("\nFork/upstream divergence:")
        git(path, "log", "--left-right", "--cherry-pick", "--oneline", f"{fork_remote_ref}...{upstream_remote_ref}")

        git(path, "switch", "-c", branch)
        merge = git(
            path,
            "merge",
            "--no-ff",
            "--no-edit",
            "-m",
            f"Merge upstream/{component['upstreamRef']} into fork/{args.fork_ref}",
            upstream_remote_ref,
            check=False,
        )
        if merge.returncode != 0:
            print(
                "\nThe merge needs manual attention. It was deliberately left in progress; "
                "inspect git status, resolve and commit the conflicts, or abort it explicitly.",
                file=sys.stderr,
            )
            return merge.returncode

        changed = git_output(path, "diff", "--name-only", f"{fork_remote_ref}..HEAD").splitlines()
        risk_paths = [item for item in changed if any(marker in item.casefold() for marker in RISK_MARKERS)]
        print(f"\nPrepared review branch: {branch}")
        print(f"Candidate fork pin: {git_output(path, 'rev-parse', 'HEAD')}")
        print(f"Changed paths: {len(changed)}")
        if risk_paths:
            print("Paths requiring integration-boundary review:")
            print("\n".join(f"  {item}" for item in risk_paths))
        print(
            "\nNext: review the merge, run the upstream standalone validation, push this branch "
            "and merge its fork PR. Then run finalize_upstream_pin.py."
        )
        return 0
    except UpstreamUpdateError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
