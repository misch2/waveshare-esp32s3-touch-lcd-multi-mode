#!/usr/bin/env python3
"""Derive the combined firmware version from the containing Git repository.

An exact annotated ``vMAJOR.MINOR.PATCH`` tag on ``HEAD`` identifies a
release.  All other builds are development builds and include the short
commit hash so that an image can be traced back to its source.  This module is
also dependency-free and keeps its PlatformIO integration at the bottom so
the version-detection functions can be imported by host-side tests.
"""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
from typing import NamedTuple, Optional


SEMVER_PATTERN = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
TAG_PATTERN = re.compile(
    r"^v(?P<version>" + SEMVER_PATTERN.pattern[1:-1] + r")$"
)


class GitVersion(NamedTuple):
    """Version and provenance values for one firmware build."""

    version: str
    commit: str
    dirty: bool
    release_tag: Optional[str]

    @property
    def is_release(self) -> bool:
        return self.release_tag is not None and not self.dirty


def _git_output(repository: Path, *arguments: str) -> str:
    """Return one Git command's output, or an empty string if Git is absent."""

    try:
        result = subprocess.run(
            ["git", *arguments],
            cwd=str(repository),
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
            text=True,
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def _working_tree_is_dirty(repository: Path) -> bool:
    """Check tracked changes while deliberately ignoring untracked files."""

    try:
        result = subprocess.run(
            [
                "git",
                "status",
                "--porcelain=v1",
                "--untracked-files=no",
            ],
            cwd=str(repository),
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
            text=True,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0 and bool(result.stdout.strip())


def _exact_annotated_release_tag(repository: Path, head: str) -> Optional[str]:
    """Find the unique SemVer annotated tag whose peeled commit is ``head``."""

    if not head:
        return None

    raw_tags = _git_output(
        repository,
        "for-each-ref",
        "--format=%(refname:short)",
        "refs/tags",
    )
    matches = []
    for tag in raw_tags.splitlines():
        if TAG_PATTERN.fullmatch(tag) is None:
            continue
        if _git_output(repository, "rev-list", "-n", "1", tag) != head:
            continue
        # Lightweight tags have type "commit"; only annotated tag objects
        # (type "tag") are valid release identifiers.
        if _git_output(repository, "cat-file", "-t", tag) != "tag":
            continue
        matches.append(tag)

    if len(matches) > 1:
        raise ValueError(
            "HEAD has multiple annotated release tags: " + ", ".join(sorted(matches))
        )
    return matches[0] if matches else None


def detect_version(repository: Path) -> GitVersion:
    """Return a deterministic version for ``repository``.

    A checkout without a usable Git repository still produces a safe,
    traceable development version rather than failing the firmware build.
    """

    repository = repository.resolve()
    head = _git_output(repository, "rev-parse", "HEAD")
    commit = _git_output(repository, "rev-parse", "--short=7", "HEAD") or "unknown"
    dirty = bool(head) and _working_tree_is_dirty(repository)
    tag = _exact_annotated_release_tag(repository, head)

    if tag is not None and not dirty:
        version = TAG_PATTERN.fullmatch(tag).group("version")
    else:
        version = "0.0.0-dev+g" + commit
        if dirty:
            version += ".dirty"
    return GitVersion(version, commit, dirty, tag)


def configure_platformio(env) -> GitVersion:
    """Inject the detected values into a PlatformIO/SCons environment."""

    project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
    repository = project_dir.parent
    info = detect_version(repository)
    print("[build_metadata] COMBINED_FIRMWARE_VERSION = " + info.version)
    print("[build_metadata] COMBINED_FIRMWARE_GIT_HASH = " + info.commit)
    env.Append(
        CPPDEFINES=[
            ("COMBINED_FIRMWARE_VERSION", env.StringifyMacro(info.version)),
            ("COMBINED_FIRMWARE_GIT_HASH", env.StringifyMacro(info.commit)),
        ]
    )
    return info


# PlatformIO evaluates this file in a SCons context where Import("env") is
# available.  Keeping that integration guarded makes ordinary Python imports
# safe for dependency-free tests and tooling.
try:
    Import("env")
except NameError:
    env = None

if env is not None:
    configure_platformio(env)


if __name__ == "__main__":
    repository_root = Path(__file__).resolve().parents[2]
    print(detect_version(repository_root).version)
