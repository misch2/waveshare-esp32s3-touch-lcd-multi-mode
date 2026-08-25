#!/usr/bin/env python3
"""Finalize one reviewed fork update in the combined repository."""

from __future__ import annotations

import argparse
import json
import sys

from upstream_update_common import (
    MANIFEST_PATH,
    REPOSITORY_ROOT,
    UpstreamUpdateError,
    component_path,
    ensure_remote,
    exact_upstream_tag,
    full_commit,
    git,
    git_output,
    load_manifest,
    require_clean_parent_files,
    require_clean_submodule,
    require_recorded_parent_pin,
    run,
    select_component,
)


GENERATED_HEADER = "firmware/lib/app_core/include/BuildProvenance.h"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fast-forward to one reviewed fork tip and stage its gitlink and provenance."
    )
    parser.add_argument("component", help="Component id, path, or display name from UPSTREAMS.json")
    parser.add_argument("--fork-ref", default="main", help="Reviewed fork branch (default: main)")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate local state and show the mutating actions without running them",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest = load_manifest()
        component = select_component(manifest, args.component)
        path = component_path(component)
        require_clean_submodule(path)
        require_recorded_parent_pin(component, path, require_head=False)
        require_clean_parent_files("UPSTREAMS.json", GENERATED_HEADER)
        ensure_remote(path, "fork", component["forkUrl"], dry_run=args.dry_run)
        ensure_remote(path, "upstream", component["upstreamUrl"], dry_run=args.dry_run)

        fork_remote_ref = f"fork/{args.fork_ref}"
        upstream_remote_ref = f"upstream/{component['upstreamRef']}"
        print(f"Component: {component['displayName']} ({component['path']})")
        print(f"Old fork pin: {component['forkPin']}")
        print(f"Old upstream base: {component['upstreamBase']}")

        if args.dry_run:
            print("\nWould fetch both remotes, fast-forward local main to the reviewed fork ref,")
            print("update only this UPSTREAMS.json entry, regenerate BuildProvenance.h,")
            print("stage those files and the gitlink, and validate all staged provenance.")
            return 0

        git(path, "fetch", "--prune", "fork")
        git(path, "fetch", "--prune", "upstream")
        fork_tip = full_commit(path, fork_remote_ref)
        full_commit(path, upstream_remote_ref)

        if git(path, "show-ref", "--verify", "--quiet", "refs/heads/main", check=False).returncode == 0:
            git(path, "switch", "main")
        else:
            git(path, "switch", "-c", "main", "--track", fork_remote_ref)
        git(path, "merge", "--ff-only", fork_remote_ref)
        new_pin = git_output(path, "rev-parse", "HEAD")
        if new_pin != fork_tip:
            raise UpstreamUpdateError(
                f"Local main is not exactly {fork_remote_ref}. Resolve the divergence explicitly."
            )
        if git(path, "merge-base", "--is-ancestor", component["forkPin"], new_pin, check=False).returncode != 0:
            raise UpstreamUpdateError("Reviewed fork tip does not descend from the recorded release pin.")
        upstream_base = git_output(path, "merge-base", new_pin, upstream_remote_ref)
        if git(path, "merge-base", "--is-ancestor", upstream_base, new_pin, check=False).returncode != 0:
            raise UpstreamUpdateError("Calculated upstream base is not an ancestor of the fork pin.")
        upstream_tag = exact_upstream_tag(path, "upstream", upstream_base)

        component["forkPin"] = new_pin
        component["upstreamBase"] = upstream_base
        if upstream_tag is None:
            component.pop("upstreamTag", None)
        else:
            component["upstreamTag"] = upstream_tag
        MANIFEST_PATH.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        run((sys.executable, "firmware/extra_scripts/generate_build_provenance.py"))
        git(
            REPOSITORY_ROOT,
            "add",
            "--",
            component["path"],
            "UPSTREAMS.json",
            GENERATED_HEADER,
        )
        run((sys.executable, "scripts/test_upstream_provenance.py"))

        print(f"\nNew fork pin: {new_pin}")
        print(f"New upstream base: {upstream_base}")
        print(f"New upstream tag: {upstream_tag or '<none>'}")
        print("Staged parent changes:")
        git(REPOSITORY_ROOT, "diff", "--cached", "--stat", "--", component["path"], "UPSTREAMS.json", GENERATED_HEADER)
        print(
            "\nNext: review the staged diff and adapter impact, run the combined validation, "
            "then perform and record the physical smoke test. Nothing was committed or pushed."
        )
        return 0
    except (KeyError, UpstreamUpdateError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
