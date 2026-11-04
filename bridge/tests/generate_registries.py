# -*- coding: utf-8 -*-
"""Generate the bridge's three registry tables from one manifest.

The bridge knows how to transcode a topic message, a service and an action.
*Which* types those are, was three hand-edited lists - one per registry -
and they could drift: the include list, the table and the "does this type
have a pretty printer" answer were three separate facts about the same type.

`registry_manifest.txt` is now the only place a type is named. This script
reads it, resolves every entry against the shim's *generated* headers (the
same headers a nav2 node compiles against, so the answer cannot be a guess),
and writes:

  * `registry_generated.hpp` - the `#include` of every registered type's
    header, so a new type needs no edit in the .cpp files at all;
  * `registry_msg_table.inc`, `registry_srv_table.inc`,
    `registry_action_table.inc` - the table entries, one per line, included
    at the spot in the registry .cpp where the factory templates are visible.

The tables are included rather than generated into a header because the
factory (`makeCodec<T>`) is a template that needs the per-type functions of
its own .cpp file - the pretty printer and the field setter, which are the
hand-written part of the registry and stay that way. A generator cannot
decide which fields of a `LaserScan` are worth printing or what an enum
value is called; those decisions stay with the people who wrote the type.

So the generator's job is narrow, and its value is that a type can no longer
be half-registered: the table, the include and the "the header exists at
all" check all come from one list, and `--check` fails when the checked-in
files no longer match the manifest.

    python bridge\\tests\\generate_registries.py            # write the files
    python bridge\\tests\\generate_registries.py --check    # fail if stale

The generated files are checked in: a build does not need Python.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = REPO_ROOT / "bridge" / "tests" / "registry_manifest.txt"
GENERATED_INCLUDE = REPO_ROOT / "nav2" / "ros2_shim" / "generated" / "include"
SIM_ACTION_HEADER = (
    REPO_ROOT / "bridge" / "windows" / "include" / "ros2_bridge" / "sim_action_messages.hpp")
OUTPUT_DIR = REPO_ROOT / "bridge" / "windows" / "include" / "ros2_bridge"

# `ros2_message_type = "sensor_msgs/msg/LaserScan"` - the constant every
# generated header carries. A service names itself with `ros2_service_type`,
# and an action has no constant of its own: its three parts carry
# `pkg/action/Name_Goal`, and the name is the goal's without the suffix.
TYPE_RE = re.compile(r'ros2_(?:message|service)_type = "([^"]+)"')
ACTION_GOAL_RE = re.compile(r'ros2_message_type = "([^"]+_Goal)"')

# `namespace geometry_msgs {` - the generated headers put the brace on the
# next line, so the brace is optional here.
NAMESPACE_RE = re.compile(r"^\s*namespace\s+([A-Za-z_]\w*)\s*\{?\s*$")
STRUCT_RE = re.compile(r"^\s*struct\s+([A-Za-z_]\w*)\s*\{?\s*$")


class Entry:
    """One registered type: its name, its C++ type and its header."""

    def __init__(self, kind: str, type_name: str, cpp_type: str, header: str) -> None:
        self.kind = kind
        self.type_name = type_name
        self.cpp_type = cpp_type
        self.header = header


def read_manifest(path: Path) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        parts = stripped.split()
        if len(parts) != 2:
            raise SystemExit("%s:%d: expected '<kind> <type>', got %r"
                             % (path, number, stripped))
        kind, type_name = parts
        if kind not in ("msg", "srv", "action"):
            raise SystemExit("%s:%d: unknown kind %r" % (path, number, kind))
        entries.append((kind, type_name))
    return entries


def cpp_type_of(text: str, header: Path) -> str:
    """The C++ name of the struct in a generated header.

    `namespace geometry_msgs { namespace msg { struct LaserScan {` gives
    `geometry_msgs::msg::LaserScan`. The service and action headers put the
    struct inside the same two namespaces, so one walk covers all three kinds.
    """
    namespaces: list[str] = []
    struct_name = ""
    for line in text.splitlines():
        namespace = NAMESPACE_RE.match(line)
        if namespace:
            namespaces.append(namespace.group(1))
        struct = STRUCT_RE.match(line)
        if struct and not struct_name:
            struct_name = struct.group(1)
            break
    if not struct_name:
        raise SystemExit("no struct found in %s" % header)
    parts = [part for part in namespaces if part != "ros2_shim"]
    return "::".join(parts + [struct_name])


def scan_generated_headers() -> dict[str, tuple[str, str]]:
    """`pkg/msg/Name` -> (cpp type, header path), from the generated headers.

    The header path is relative to the include root, which is exactly how it
    is spelled in an `#include`.
    """
    found: dict[str, tuple[str, str]] = {}
    for header in sorted(GENERATED_INCLUDE.rglob("*.hpp")):
        text = header.read_text(encoding="utf-8")
        if "ros2_message_tag" not in text:
            continue
        relative = header.relative_to(GENERATED_INCLUDE).as_posix()
        cpp_type = cpp_type_of(text, header)
        for match in TYPE_RE.finditer(text):
            name = match.group(1)
            if name.endswith("_Request") or name.endswith("_Response"):
                # The half types of a service; the service itself is named by
                # `ros2_service_type`, which the same pattern matches.
                continue
            found.setdefault(name, (cpp_type, relative))
        for match in ACTION_GOAL_RE.finditer(text):
            name = match.group(1)[: -len("_Goal")]
            found.setdefault(name, (cpp_type, relative))
    return found


def sim_action_types() -> dict[str, tuple[str, str]]:
    """The hand-written simulator actions, which are not generated.

    `ros2_bridge_sim/action/Drive` lives in `sim_action_messages.hpp` and has
    no counterpart under `generated/include`, so it is resolved here.
    """
    text = SIM_ACTION_HEADER.read_text(encoding="utf-8")
    found: dict[str, tuple[str, str]] = {}
    for match in ACTION_GOAL_RE.finditer(text):
        name = match.group(1)[: -len("_Goal")]
        found[name] = ("ros2_bridge_sim::action::Drive", "ros2_bridge/sim_action_messages.hpp")
    return found


def resolve(entries: list[tuple[str, str]]) -> list[Entry]:
    generated = scan_generated_headers()
    generated.update(sim_action_types())
    resolved: list[Entry] = []
    seen: set[tuple[str, str]] = set()
    for kind, type_name in entries:
        if (kind, type_name) in seen:
            raise SystemExit("%s listed twice" % type_name)
        seen.add((kind, type_name))
        if type_name not in generated:
            raise SystemExit(
                "%s is not in the generated headers - the type does not exist "
                "on this side of the bridge" % type_name)
        cpp_type, header = generated[type_name]
        resolved.append(Entry(kind, type_name, cpp_type, header))
    return resolved


def render_includes(entries: list[Entry]) -> str:
    out: list[str] = []
    out.append("// Generated by bridge/tests/generate_registries.py - do not edit.")
    out.append("//")
    out.append("// The source of truth is bridge/tests/registry_manifest.txt; run")
    out.append("// `python bridge\\\\tests\\\\generate_registries.py` after changing it.")
    out.append("//")
    out.append("// The headers of every registered type, so that adding a type to the")
    out.append("// manifest needs no edit in the registry .cpp files.")
    out.append("#ifndef ROS2_BRIDGE_REGISTRY_GENERATED_HPP")
    out.append("#define ROS2_BRIDGE_REGISTRY_GENERATED_HPP")
    out.append("")
    seen: set[str] = set()
    for entry in entries:
        if entry.header in seen:
            continue
        seen.add(entry.header)
        out.append('#include "%s"' % entry.header)
    out.append("")
    out.append("#endif  // ROS2_BRIDGE_REGISTRY_GENERATED_HPP")
    out.append("")
    return "\n".join(out)


def render_table(kind: str, entries: list[Entry]) -> str:
    """The table entries of one registry, one per line.

    The entry names the type twice on purpose: once as the key the graph
    carries and once as the C++ template argument. The key is what a peer
    sends; the template argument is what the payload is transposed into.
    """
    factory = {"msg": "makeCodec", "srv": "makeService", "action": "makeAction"}[kind]
    out: list[str] = []
    out.append("// Generated by bridge/tests/generate_registries.py - do not edit.")
    out.append("//")
    out.append("// The source of truth is bridge/tests/registry_manifest.txt; run")
    out.append("// `python bridge\\\\tests\\\\generate_registries.py` after changing it.")
    for entry in entries:
        if entry.kind != kind:
            continue
        out.append('{"%s", %s<%s>("%s")},'
                   % (entry.type_name, factory, entry.cpp_type, entry.type_name))
    out.append("")
    return "\n".join(out)


OUTPUTS = {
    "msg": OUTPUT_DIR / "registry_msg_table.inc",
    "srv": OUTPUT_DIR / "registry_srv_table.inc",
    "action": OUTPUT_DIR / "registry_action_table.inc",
}
INCLUDES_OUTPUT = OUTPUT_DIR / "registry_generated.hpp"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true",
                        help="fail when the checked-in files do not match the manifest")
    parser.add_argument("--manifest", default=str(MANIFEST))
    args = parser.parse_args()

    entries = resolve(read_manifest(Path(args.manifest)))
    counts = {"msg": 0, "srv": 0, "action": 0}
    for entry in entries:
        counts[entry.kind] += 1
    print("manifest: %d msg, %d srv, %d action"
          % (counts["msg"], counts["srv"], counts["action"]))

    files = {INCLUDES_OUTPUT: render_includes(entries)}
    for kind, path in OUTPUTS.items():
        files[path] = render_table(kind, entries)

    stale = []
    for path, text in files.items():
        current = path.read_text(encoding="utf-8") if path.exists() else ""
        if current == text:
            continue
        if args.check:
            stale.append(path)
            continue
        path.write_text(text, encoding="utf-8")
        print("wrote %s" % path.relative_to(REPO_ROOT))

    if stale:
        for path in stale:
            print("ERROR: %s is out of date; run"
                  " `python bridge/tests/generate_registries.py`" % path.relative_to(REPO_ROOT))
        return 1
    if args.check:
        print("generated files are up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
