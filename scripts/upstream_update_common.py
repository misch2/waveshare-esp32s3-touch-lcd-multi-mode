#!/usr/bin/env python3
"""Shared, dependency-free helpers for the upstream update scripts."""

from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path
from typing import Any, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = REPOSITORY_ROOT / "UPSTREAMS.json"


class UpstreamUpdateError(RuntimeError):
    """An expected, user-actionable upstream update failure."""


def load_manifest() -> dict[str, Any]:
    try:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UpstreamUpdateError(f"Cannot read {MANIFEST_PATH.name}: {error}") from error

    components = manifest.get("components")
    if manifest.get("schemaVersion") != 1 or not isinstance(components, list):
        raise UpstreamUpdateError(
            "UPSTREAMS.json must contain schemaVersion 1 and a components array."
        )
    return manifest


def select_component(manifest: dict[str, Any], selector: str) -> dict[str, str]:
    normalized = selector.casefold()
    matches = [
        component
        for component in manifest["components"]
        if normalized
        in {
            str(component.get("id", "")).casefold(),
            str(component.get("path", "")).casefold(),
            str(component.get("displayName", "")).casefold(),
        }
    ]
    if len(matches) != 1:
        choices = ", ".join(component["id"] for component in manifest["components"])
        raise UpstreamUpdateError(
            f"Unknown component '{selector}'. Choose exactly one of: {choices}."
        )

    required = (
        "id",
        "path",
        "displayName",
        "upstreamUrl",
        "upstreamRef",
        "upstreamBase",
        "forkUrl",
        "forkPin",
    )
    component = matches[0]
    missing = [key for key in required if not isinstance(component.get(key), str)]
    if missing:
        raise UpstreamUpdateError(
            f"Component '{component.get('id', selector)}' is missing: {', '.join(missing)}."
        )
    upstream_tag = component.get("upstreamTag")
    if upstream_tag is not None and (not isinstance(upstream_tag, str) or not upstream_tag):
        raise UpstreamUpdateError(
            f"Component '{component.get('id', selector)}' has an invalid upstreamTag; "
            "omit it when the exact upstream commit has no tag."
        )
    return component


def component_path(component: dict[str, str]) -> Path:
    path = (REPOSITORY_ROOT / component["path"]).resolve()
    try:
        path.relative_to(REPOSITORY_ROOT.resolve())
    except ValueError as error:
        raise UpstreamUpdateError(
            f"Component path escapes the repository: {component['path']}"
        ) from error
    if not path.is_dir():
        raise UpstreamUpdateError(
            f"Submodule '{component['path']}' is not initialized. Run git submodule update --init."
        )
    return path


def run(
    command: Sequence[str],
    *,
    cwd: Path = REPOSITORY_ROOT,
    capture: bool = False,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        list(command),
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        suffix = f": {detail}" if detail else ""
        raise UpstreamUpdateError(f"Command failed: {' '.join(command)}{suffix}")
    return result


def git(
    path: Path,
    *arguments: str,
    capture: bool = False,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    return run(("git", "-C", str(path), *arguments), capture=capture, check=check)


def git_output(path: Path, *arguments: str) -> str:
    return git(path, *arguments, capture=True).stdout.strip()


def require_clean_submodule(path: Path) -> None:
    status = git_output(path, "status", "--porcelain")
    if status:
        raise UpstreamUpdateError(
            "The selected submodule has uncommitted changes. Commit or remove them before continuing:\n"
            + status
        )
    for state in ("MERGE_HEAD", "REBASE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD"):
        if git(path, "rev-parse", "--verify", "--quiet", state, check=False).returncode == 0:
            raise UpstreamUpdateError(
                f"The selected submodule has an unfinished {state.removesuffix('_HEAD').lower()} operation."
            )
    for state_directory in ("rebase-apply", "rebase-merge"):
        state_path = Path(git_output(path, "rev-parse", "--git-path", state_directory))
        if not state_path.is_absolute():
            state_path = path / state_path
        if state_path.exists():
            raise UpstreamUpdateError("The selected submodule has an unfinished rebase operation.")


def normalized_url(url: str) -> str:
    return url.strip().rstrip("/").removesuffix(".git")


def ensure_remote(path: Path, name: str, expected_url: str, *, dry_run: bool) -> None:
    result = git(path, "remote", "get-url", name, capture=True, check=False)
    if result.returncode != 0:
        if dry_run:
            print(f"Would add remote {name}: {expected_url}")
        else:
            git(path, "remote", "add", name, expected_url)
            print(f"Added remote {name}: {expected_url}")
        return

    actual_url = result.stdout.strip()
    if normalized_url(actual_url) != normalized_url(expected_url):
        raise UpstreamUpdateError(
            f"Remote '{name}' points to {actual_url}, expected {expected_url}. "
            "Correct it explicitly before continuing."
        )


def require_clean_parent_files(*paths: str) -> None:
    status = git_output(REPOSITORY_ROOT, "status", "--porcelain", "--", *paths)
    if status:
        raise UpstreamUpdateError(
            "Refusing to overwrite existing parent-repository changes:\n" + status
        )


def full_commit(path: Path, revision: str) -> str:
    return git_output(path, "rev-parse", "--verify", f"{revision}^{{commit}}")


def exact_upstream_tags(path: Path, remote: str, revision: str) -> list[str]:
    """Return official remote tags that point exactly at *revision*.

    This deliberately queries the configured upstream remote instead of
    inspecting local ``refs/tags``. A tag fetched from the fork must never be
    recorded as an upstream release tag by accident. Annotated tags expose a
    tag object followed by a peeled commit in ``ls-remote``; lightweight tags
    expose only the commit, so both forms are handled here.
    """
    target = full_commit(path, revision).casefold()
    result = git(path, "ls-remote", "--tags", remote, capture=True)
    direct: dict[str, str] = {}
    peeled: dict[str, str] = {}
    for line in result.stdout.splitlines():
        object_name, separator, reference = line.partition("\t")
        if not separator or not reference.startswith("refs/tags/"):
            continue
        is_peeled = reference.endswith("^{}")
        tag_name = reference[len("refs/tags/") : -3 if is_peeled else None]
        if not tag_name:
            continue
        if is_peeled:
            peeled[tag_name] = object_name.casefold()
        else:
            direct[tag_name] = object_name.casefold()

    matches = sorted(
        tag_name
        for tag_name in set(direct) | set(peeled)
        if (peeled.get(tag_name) or direct.get(tag_name)) == target
    )
    return matches


def exact_upstream_tag(path: Path, remote: str, revision: str) -> str | None:
    """Return the one exact official tag, or ``None`` when there is no tag."""
    matches = exact_upstream_tags(path, remote, revision)
    if len(matches) > 1:
        raise UpstreamUpdateError(
            f"Multiple tags on official upstream point exactly at {full_commit(path, revision)}: "
            + ", ".join(matches)
            + ". Record a single unambiguous upstream release explicitly."
        )
    return matches[0] if matches else None


def staged_gitlink(component: dict[str, str]) -> str:
    staged = git_output(
        REPOSITORY_ROOT, "ls-files", "--stage", "--", component["path"]
    )
    match = re.match(r"^160000 ([0-9a-f]{40}) \d+\t", staged)
    if not match:
        raise UpstreamUpdateError(
            f"Cannot read staged gitlink for {component['path']}."
        )
    return match.group(1)


def require_recorded_parent_pin(component: dict[str, str], path: Path, *, require_head: bool) -> None:
    index_pin = staged_gitlink(component)
    if index_pin != component["forkPin"]:
        raise UpstreamUpdateError(
            f"{component['id']}: parent index gitlink {index_pin} does not match "
            f"recorded forkPin {component['forkPin']}."
        )
    if require_head:
        checked_out = git_output(path, "rev-parse", "HEAD")
        if checked_out != component["forkPin"]:
            raise UpstreamUpdateError(
                f"{component['id']}: checked-out HEAD {checked_out} does not match the recorded pin."
            )
