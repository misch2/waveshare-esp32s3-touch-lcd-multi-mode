#!/usr/bin/env python3
"""Check exact upstream-tag provenance and its host diagnostics/UI contract.

This test is intentionally dependency-free.  It validates tags against the
checked-out submodule objects, so it does not infer a tag from a nearby commit
or contact the network.
"""

from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import sys
from types import SimpleNamespace

import upstream_update_common
from upstream_update_common import UpstreamUpdateError


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
EXPECTED_TAGS = {
    "meteo-plane-radar": "v0.6.3",
    "waveshare-hodiny": "v1.5.5",
}


def git_output(path: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(path), *arguments],
        cwd=REPOSITORY_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def fail(message: str) -> None:
    raise AssertionError(message)


def test_manifest_and_submodule_tags() -> None:
    manifest = json.loads(
        (REPOSITORY_ROOT / "UPSTREAMS.json").read_text(encoding="utf-8")
    )
    components = {component["id"]: component for component in manifest["components"]}

    if set(components) != set(EXPECTED_TAGS):
        fail(f"Unexpected component IDs: {sorted(components)}")

    for component_id, expected_tag in EXPECTED_TAGS.items():
        component = components[component_id]
        actual_tag = component.get("upstreamTag")
        if actual_tag != expected_tag:
            fail(
                f"{component_id}: expected exact upstreamTag {expected_tag!r}, "
                f"got {actual_tag!r}"
            )

        submodule = REPOSITORY_ROOT / component["path"]
        tagged_commit = git_output(
            submodule,
            "rev-parse",
            "--verify",
            f"refs/tags/{actual_tag}^{{commit}}",
        )
        if tagged_commit != component["upstreamBase"]:
            fail(
                f"{component_id}: tag {actual_tag} resolves to {tagged_commit}, "
                f"not the recorded exact base {component['upstreamBase']}"
            )

        # A tag on a descendant/ancestor is not an exact release label.  The
        # equality check above is the important rule; this also documents that
        # the commit's complete points-at set may contain more than one valid
        # tag without changing the immutable commit identity.
        tags_at_commit = git_output(submodule, "tag", "--points-at", component["upstreamBase"])
        if actual_tag not in tags_at_commit.splitlines():
            fail(f"{component_id}: exact tag is not in git tag --points-at output")


def test_exact_tag_parser_and_ambiguity_without_network() -> None:
    target = "a" * 40
    annotated_tag_object = "b" * 40
    remote_tags = "\n".join(
        (
            f"{target}\trefs/tags/v0.6.3",
            f"{annotated_tag_object}\trefs/tags/v1.5.5",
            f"{target}\trefs/tags/v1.5.5^{{}}",
            f"{'c' * 40}\trefs/tags/not-this-commit",
        )
    )

    original_full_commit = upstream_update_common.full_commit
    original_git = upstream_update_common.git

    def fake_git(path, *arguments, capture=False, check=True):
        del path, check
        if not capture or arguments != ("ls-remote", "--tags", "upstream"):
            raise AssertionError(f"unexpected git invocation: {arguments!r}")
        return SimpleNamespace(stdout=remote_tags)

    upstream_update_common.full_commit = lambda path, revision: target
    upstream_update_common.git = fake_git
    try:
        matches = upstream_update_common.exact_upstream_tags(
            Path("unused-submodule"), "upstream", "main"
        )
        if matches != ["v0.6.3", "v1.5.5"]:
            fail(f"Unexpected exact tag matches: {matches!r}")

        try:
            upstream_update_common.exact_upstream_tag(
                Path("unused-submodule"), "upstream", "main"
            )
        except UpstreamUpdateError as error:
            if "Multiple tags" not in str(error):
                fail(f"Ambiguous tag error was not descriptive: {error}")
        else:
            fail("Multiple exact tags were accepted as an unambiguous tag")

        upstream_update_common.git = lambda path, *arguments, **kwargs: SimpleNamespace(
            stdout=f"{target}\trefs/tags/v0.6.3\n"
        )
        if upstream_update_common.exact_upstream_tags(
            Path("unused-submodule"), "upstream", "main"
        ) != ["v0.6.3"]:
            fail("Single exact tag was not retained")
    finally:
        upstream_update_common.full_commit = original_full_commit
        upstream_update_common.git = original_git


def test_generated_dto_and_diagnostics_contract() -> None:
    header = (
        REPOSITORY_ROOT / "firmware/lib/app_core/include/BuildProvenance.h"
    ).read_text(encoding="utf-8")
    if "const char* upstreamTag;" not in header:
        fail("BuildProvenance DTO does not expose upstreamTag")
    for expected_tag in EXPECTED_TAGS.values():
        if f'"{expected_tag}"' not in header:
            fail(f"Generated BuildProvenance.h does not contain {expected_tag}")

    diagnostics_source = (
        REPOSITORY_ROOT / "firmware/src/main.cpp"
    ).read_text(encoding="utf-8")
    if "upstreamTag" not in diagnostics_source:
        fail("Diagnostics serialization does not include upstreamTag")


def test_optional_tag_generation_and_conditional_ui() -> None:
    generator = (
        REPOSITORY_ROOT / "firmware/extra_scripts/generate_build_provenance.py"
    ).read_text(encoding="utf-8")
    if not re.search(r"get\(\s*[\"']upstreamTag[\"']\s*\)", generator):
        fail("Provenance generator does not treat upstreamTag as optional")
    if not re.search(r"[\"']nullptr[\"']\s+if\s+upstream_tag\s+is\s+None", generator):
        fail("Provenance generator does not preserve a missing upstreamTag as null")

    page = (
        REPOSITORY_ROOT / "firmware/lib/web_host/src/HostWebPage.h"
    ).read_text(encoding="utf-8")
    if "upstreamTag" not in page:
        fail("Component versions UI does not read upstreamTag")
    if not re.search(
        r"if\s*\(\s*typeof\s+component\.upstreamTag\s*===\s*['\"]string['\"]"
        r"\s*&&\s*component\.upstreamTag\s*\)",
        page,
    ):
        fail("Component versions UI does not append a tag only when it is non-empty")
    if "commitLink(component.upstreamUrl, component.upstreamBase, shortSha(component.upstreamBase))" not in page:
        fail("Component versions UI no longer renders/links the upstream commit SHA")

    diagnostics_source = (
        REPOSITORY_ROOT / "firmware/src/main.cpp"
    ).read_text(encoding="utf-8")
    if not re.search(
        r"source\.upstreamTag\s*!=\s*nullptr\s*&&\s*source\.upstreamTag\[0\]\s*!=\s*['\"]\\0['\"]",
        diagnostics_source,
    ):
        fail("Diagnostics do not omit a missing or empty upstreamTag")


def main() -> int:
    tests = (
        test_manifest_and_submodule_tags,
        test_exact_tag_parser_and_ambiguity_without_network,
        test_generated_dto_and_diagnostics_contract,
        test_optional_tag_generation_and_conditional_ui,
    )
    try:
        for test in tests:
            test()
    except (AssertionError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("Upstream tag provenance contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
