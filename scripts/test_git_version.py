#!/usr/bin/env python3
"""Tests for the Git-derived firmware version helper.

The fixtures are disposable Git repositories.  This keeps the tests
independent of the tags and worktree state of the checkout running them, and
also exercises the same Git plumbing used by PlatformIO builds.
"""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import ModuleType


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
VERSION_SCRIPT = REPOSITORY_ROOT / "firmware" / "extra_scripts" / "git_version.py"


def fail(message: str) -> None:
    raise AssertionError(message)


def load_version_module() -> ModuleType:
    if not VERSION_SCRIPT.exists():
        fail(f"Missing Git version helper: {VERSION_SCRIPT}")
    spec = importlib.util.spec_from_file_location("combined_git_version", VERSION_SCRIPT)
    if spec is None or spec.loader is None:
        fail(f"Unable to load Git version helper: {VERSION_SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    # dataclasses (used by GitVersion) resolve the class module through
    # sys.modules while the module is being executed.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def git(repository: Path, *arguments: str) -> str:
    command = ["git", "-C", str(repository), *arguments]
    result = subprocess.run(
        command,
        check=True,
        capture_output=True,
        text=True,
        env={
            **os.environ,
            # Do not let a developer's global Git configuration affect the
            # disposable repositories or make commits fail on a clean host.
            "GIT_CONFIG_NOSYSTEM": "1",
        },
    )
    return result.stdout.strip()


def create_repository() -> tuple[tempfile.TemporaryDirectory[str], Path, str]:
    temporary_directory = tempfile.TemporaryDirectory(prefix="combined-git-version-")
    repository = Path(temporary_directory.name)
    git(repository, "init", "--quiet")
    git(repository, "config", "user.name", "Combined firmware tests")
    git(repository, "config", "user.email", "combined-firmware-tests@example.invalid")
    (repository / "tracked.txt").write_text("initial\n", encoding="utf-8")
    git(repository, "add", "tracked.txt")
    git(repository, "commit", "--quiet", "-m", "initial")
    commit = git(repository, "rev-parse", "HEAD")
    return temporary_directory, repository, commit


def short_sha(commit: str) -> str:
    return commit[:7]


def version_for(module: ModuleType, repository: Path) -> str:
    function = getattr(module, "detect_version", None)
    if not callable(function):
        fail("git_version.py must expose detect_version(repository)")
    try:
        result = function(repository)
    except TypeError as error:
        raise AssertionError(
            "detect_version must accept a repository path for testing"
        ) from error
    version = getattr(result, "version", None)
    if not isinstance(version, str):
        fail("detect_version(repository) must return an object with a string version")
    return version


def test_exact_annotated_semver_tag() -> None:
    temporary_directory, repository, _ = create_repository()
    try:
        git(repository, "tag", "-a", "v1.1.0", "-m", "Release 1.1.0")
        module = load_version_module()
        actual = version_for(module, repository)
        if actual != "1.1.0":
            fail(f"Annotated v1.1.0 was not resolved as 1.1.0: {actual!r}")
    finally:
        temporary_directory.cleanup()


def test_lightweight_tag_is_not_a_release() -> None:
    temporary_directory, repository, commit = create_repository()
    try:
        git(repository, "tag", "v1.1.0")
        module = load_version_module()
        actual = version_for(module, repository)
        expected = f"0.0.0-dev+g{short_sha(commit)}"
        if actual != expected:
            fail(
                "A lightweight v1.1.0 tag must not be treated as a release: "
                f"expected {expected!r}, got {actual!r}"
            )
    finally:
        temporary_directory.cleanup()


def test_non_semver_annotated_tag_is_rejected() -> None:
    temporary_directory, repository, _ = create_repository()
    try:
        git(repository, "tag", "-a", "v1.1", "-m", "Invalid release tag")
        module = load_version_module()
        actual = version_for(module, repository)
        expected = f"0.0.0-dev+g{short_sha(git(repository, 'rev-parse', 'HEAD'))}"
        if actual != expected:
            fail(
                "Non-SemVer annotated tag v1.1 must not be treated as a release: "
                f"expected {expected!r}, got {actual!r}"
            )
    finally:
        temporary_directory.cleanup()


def test_untagged_commit_uses_dev_version_and_short_sha() -> None:
    temporary_directory, repository, commit = create_repository()
    try:
        module = load_version_module()
        actual = version_for(module, repository)
        expected = f"0.0.0-dev+g{short_sha(commit)}"
        if actual != expected:
            fail(f"Unexpected untagged version: expected {expected!r}, got {actual!r}")
    finally:
        temporary_directory.cleanup()


def test_dirty_worktree_adds_dirty_suffix() -> None:
    temporary_directory, repository, commit = create_repository()
    try:
        (repository / "tracked.txt").write_text("changed\n", encoding="utf-8")
        module = load_version_module()
        actual = version_for(module, repository)
        expected = f"0.0.0-dev+g{short_sha(commit)}.dirty"
        if actual != expected:
            fail(f"Unexpected dirty version: expected {expected!r}, got {actual!r}")
    finally:
        temporary_directory.cleanup()


def test_dirty_release_tag_is_a_development_build() -> None:
    temporary_directory, repository, commit = create_repository()
    try:
        git(repository, "tag", "-a", "v1.1.0", "-m", "Release 1.1.0")
        (repository / "tracked.txt").write_text("changed\n", encoding="utf-8")
        module = load_version_module()
        actual = version_for(module, repository)
        expected = f"0.0.0-dev+g{short_sha(commit)}.dirty"
        if actual != expected:
            fail(
                "A dirty tagged checkout must not claim the release version: "
                f"expected {expected!r}, got {actual!r}"
            )
    finally:
        temporary_directory.cleanup()


def test_multiple_annotated_release_tags_are_rejected() -> None:
    temporary_directory, repository, _ = create_repository()
    try:
        git(repository, "tag", "-a", "v1.1.0", "-m", "Release 1.1.0")
        git(repository, "tag", "-a", "v1.1.1", "-m", "Release 1.1.1")
        module = load_version_module()
        try:
            version_for(module, repository)
        except ValueError as error:
            if "multiple annotated release tags" not in str(error):
                fail(f"Unexpected ambiguity error: {error}")
        else:
            fail("Multiple annotated release tags on HEAD must be rejected")
    finally:
        temporary_directory.cleanup()


def test_missing_git_repository_has_safe_fallback() -> None:
    module = load_version_module()
    with tempfile.TemporaryDirectory(prefix="combined-git-version-missing-") as directory:
        missing_repository = Path(directory) / "no-repository"
        actual = version_for(module, missing_repository)
        if actual != "0.0.0-dev+gunknown":
            fail(
                "Unexpected no-Git fallback: expected '0.0.0-dev+gunknown', "
                f"got {actual!r}"
            )


def main() -> int:
    tests = (
        test_exact_annotated_semver_tag,
        test_lightweight_tag_is_not_a_release,
        test_non_semver_annotated_tag_is_rejected,
        test_untagged_commit_uses_dev_version_and_short_sha,
        test_dirty_worktree_adds_dirty_suffix,
        test_dirty_release_tag_is_a_development_build,
        test_multiple_annotated_release_tags_are_rejected,
        test_missing_git_repository_has_safe_fallback,
    )
    try:
        for test in tests:
            test()
    except (AssertionError, OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("Git firmware version contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
