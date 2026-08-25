try:
    Import("env")
except NameError:
    # Allow the same deterministic generator to be run by CI or developers
    # before the PlatformIO/SCons environment is loaded.
    env = None

import json
from pathlib import Path


if env is not None:
    root = Path(env.subst("$PROJECT_DIR")).resolve().parent
else:
    root = Path(__file__).resolve().parents[2]
manifest_path = root / "UPSTREAMS.json"
header_path = root / "firmware" / "lib" / "app_core" / "include" / "BuildProvenance.h"


def quote(value):
    return json.dumps(value, ensure_ascii=True)


def render(manifest):
    components = manifest["components"]
    lines = [
        "// Generated from UPSTREAMS.json; do not edit by hand.",
        "#pragma once",
        "",
        "#include <cstddef>",
        "",
        "namespace app_core {",
        "",
        "struct ComponentProvenance {",
        "  const char* id;",
        "  const char* displayName;",
        "  const char* upstreamUrl;",
        "  const char* upstreamRef;",
        "  const char* upstreamBase;",
        "  const char* upstreamTag;",
        "  const char* forkUrl;",
        "  const char* forkPin;",
        "};",
        "",
        "inline constexpr ComponentProvenance kComponentProvenance[] = {",
    ]
    for component in components:
        upstream_tag = component.get("upstreamTag")
        if upstream_tag is not None and (not isinstance(upstream_tag, str) or not upstream_tag):
            raise ValueError(
                f"{component.get('id', '<unknown>')}: upstreamTag must be a non-empty string when present"
            )
        values = (
            "id", "displayName", "upstreamUrl", "upstreamRef", "upstreamBase",
            "forkUrl", "forkPin")
        rendered_tag = "nullptr" if upstream_tag is None else quote(upstream_tag)
        rendered_values = [quote(component[field]) for field in values]
        rendered_values.insert(5, rendered_tag)
        lines.append("    {" + ", ".join(rendered_values) + "},")
    lines.extend([
        "};",
        "inline constexpr std::size_t kComponentProvenanceCount =",
        "    sizeof(kComponentProvenance) / sizeof(kComponentProvenance[0]);",
        "",
        "}  // namespace app_core",
        "",
    ])
    return "\n".join(lines)


manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
rendered = render(manifest)
if not header_path.exists() or header_path.read_text(encoding="utf-8") != rendered:
    header_path.write_text(rendered, encoding="utf-8", newline="\n")
