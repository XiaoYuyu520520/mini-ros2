#!/usr/bin/env python3
"""Generate plain C++ message structs from ROS2 .msg/.srv/.action files.

The generated headers intentionally mirror the ROS2 include paths and
namespaces (e.g. #include "nav_msgs/msg/path.hpp", nav_msgs::msg::Path) so that
nav2 source can be compiled with only a rclcpp compatibility shim on top.

Every generated struct carries a trivial `visit(Archive&)` method which lets the
runtime serialize it into the shared-memory transport without any code
generation of converters.
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

BUILTIN = {
    "bool", "byte", "char", "wchar",
    "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64",
    "float32", "float64", "string", "wstring",
}

CPP_SCALAR = {
    "bool": "bool",
    "byte": "uint8_t",
    "char": "uint8_t",
    "wchar": "uint16_t",
    "int8": "int8_t",
    "uint8": "uint8_t",
    "int16": "int16_t",
    "uint16": "uint16_t",
    "int32": "int32_t",
    "uint32": "uint32_t",
    "int64": "int64_t",
    "uint64": "uint64_t",
    "float32": "float",
    "float64": "double",
    "string": "std::string",
    "wstring": "std::u16string",
}

def camel_to_snake(name: str) -> str:
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1).lower()


@dataclass
class Field:
    base_type: str
    name: str
    kind: str  # "single" | "seq" | "array"
    size: int = 0
    cpp_type: str = ""
    include: Optional[str] = None


@dataclass
class Constant:
    cpp_type: str
    name: str
    literal: str


@dataclass
class Message:
    package: str
    name: str
    fields: List[Field] = field(default_factory=list)
    constants: List[Constant] = field(default_factory=list)
    kind: str = "msg"  # msg | srv_request | srv_response | action_goal | ...
    parent: Optional[str] = None


# What a field holds when nobody assigns one, as rosidl zeroes it. Consumers
# rely on this: slam_toolbox's `getOdomPose` fills in a TransformStamped's
# `header` and `rotation.w` and expects the rest to be the identity transform,
# so a struct that instead hands back whatever was left on the stack turns
# every odometry lookup into a different answer per stack layout.
SCALAR_DEFAULT = {
    "bool": "false",
    "string": "",
    "wstring": "",
    "float32": "0.0f",
    "float64": "0.0",
}


# The default member initializer for a field, or nothing when it needs none.
def field_default(f: Field) -> str:
    if f.kind == "array":
        # std::array default-initializes its elements, so the zeros have to be
        # asked for; a vector is already empty and knows it.
        return " = {}"
    if f.kind != "single" or f.base_type not in CPP_SCALAR:
        return ""
    # A scalar with no entry of its own is an integer, and std::string is ""
    # already, so it is left alone.
    default = SCALAR_DEFAULT.get(f.base_type, "0")
    return f" = {default}" if default else ""


def strip_comment(line: str) -> str:
    out = []
    in_str = False
    for ch in line:
        if ch == '"':
            in_str = not in_str
        if ch == "#" and not in_str:
            break
        out.append(ch)
    return "".join(out).strip()


def parse_type_spec(spec: str) -> Tuple[str, str, int]:
    spec = spec.strip()
    if "[" not in spec and "<=" in spec:
        spec = spec.split("<=", 1)[0].strip()
    if "[" not in spec:
        return spec, "single", 0
    base, _, rest = spec.partition("[")
    rest = rest.rstrip("]").strip()
    if rest == "" or rest.startswith("<"):
        return base.strip(), "seq", 0
    if rest.startswith("<="):
        return base.strip(), "seq", int(rest[2:])
    return base.strip(), "array", int(rest)


def resolve_type(pkg: str, base: str) -> Tuple[str, str]:
    if base in BUILTIN:
        return CPP_SCALAR[base], None
    if "/" in base:
        dep_pkg, dep_name = base.split("/", 1)
    else:
        dep_pkg, dep_name = pkg, base
    snake = camel_to_snake(dep_name)
    return f"::{dep_pkg}::msg::{dep_name}", f"{dep_pkg}/msg/{snake}.hpp"


def parse_constant(pkg: str, line: str) -> Optional[Constant]:
    if "=" not in line:
        return None
    lhs, _, rhs = line.partition("=")
    lhs = lhs.strip()
    rhs = rhs.strip()
    parts = lhs.split()
    if len(parts) != 2:
        return None
    type_spec, cname = parts
    base, _, _ = parse_type_spec(type_spec)
    ctype, _ = resolve_type(pkg, base)
    if base == "bool":
        literal = "true" if rhs in ("1", "true", "True") else "false"
    elif base in ("float32",):
        literal = _float_literal(rhs, single=True)
    elif base in ("float64",):
        literal = _float_literal(rhs, single=False)
    elif base in ("string", "wstring"):
        val = rhs.strip()
        if len(val) >= 2 and val[0] == '"' and val[-1] == '"':
            val = val[1:-1]
        literal = '"' + val.replace("\\", "\\\\").replace('"', '\\"') + '"'
    else:
        literal = rhs
    return Constant(ctype, cname, literal)


# Constant names that a platform header already defines as a macro, and which
# therefore cannot be declared as they stand.
#
# `windows.h` - which the shim pulls in from `ros2_shim/graph.hpp`, before any
# generated header - defines `ERROR` in `wingdi.h`. `rcl_interfaces/msg/Log`
# declares `static constexpr uint8_t ERROR = 40;`, and with the macro in scope
# that line preprocesses into `40 = 40`. The macro is dropped in the header that
# declares such a constant: one file, at the one point where the two names
# actually meet, rather than in the hundreds of sources that include both.
#
# This is the same list `include/ros2_shim/prelude.hpp` undefines for the nav2
# sources it cannot touch - lowercase `near`/`far` are left out, because ROS2
# constants are upper case and those two are `#define`d away by `NOMINMAX`-style
# settings in more places than they are used.
PLATFORM_MACRO_NAMES = (
    "ERROR",
    "DELETE",
    "IN",
    "OUT",
    "OPTIONAL",
    "TRANSPARENT",
)


def macro_guard_lines(m: "Message") -> List[str]:
    """`#undef` lines for the constants of `m` that a platform macro owns."""
    guarded = [c.name for c in m.constants if c.name in PLATFORM_MACRO_NAMES]
    if not guarded:
        return []
    lines = [
        "// These constants share their names with macros a platform header "
        "defines",
        "// (`ERROR` is one in `windows.h`), and a declaration is rewritten by "
        "the macro",
        "// rather than compiled when one is in scope. Dropped here, once, "
        "instead of in",
        "// every translation unit that includes both.",
    ]
    for name in guarded:
        lines += [f"#ifdef {name}", f"#  undef {name}", f"#endif"]
    lines.append("")
    return lines


def _float_literal(rhs: str, single: bool) -> str:
    suffix = "f" if single else ""
    low = rhs.lower()
    if low in ("nan",):
        return f"std::numeric_limits<{'float' if single else 'double'}>::quiet_NaN()"
    if low in ("inf", "+inf"):
        return f"std::numeric_limits<{'float' if single else 'double'}>::infinity()"
    if low == "-inf":
        return f"-std::numeric_limits<{'float' if single else 'double'}>::infinity()"
    if "." not in rhs and "e" not in low and "x" not in low:
        rhs = rhs + ".0"
    return rhs + suffix


def parse_fields(pkg: str, text: str, msg: Message) -> None:
    for raw in text.splitlines():
        line = strip_comment(raw)
        if not line:
            continue
        const = parse_constant(pkg, line)
        if const:
            msg.constants.append(const)
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        type_spec = parts[0]
        fname = parts[1]
        base, kind, size = parse_type_spec(type_spec)
        cpp, inc = resolve_type(pkg, base)
        field = Field(base_type=base, name=fname, kind=kind, size=size,
                      cpp_type=cpp, include=inc)
        msg.fields.append(field)


def load_package_messages(pkg_dir: str, package: str) -> List[Message]:
    messages: List[Message] = []
    msg_dir = os.path.join(pkg_dir, "msg")
    if os.path.isdir(msg_dir):
        for fname in sorted(os.listdir(msg_dir)):
            if not fname.endswith(".msg"):
                continue
            name = fname[:-4]
            text = open(os.path.join(msg_dir, fname), encoding="utf-8").read()
            m = Message(package=package, name=name, kind="msg")
            parse_fields(package, text, m)
            messages.append(m)

    srv_dir = os.path.join(pkg_dir, "srv")
    if os.path.isdir(srv_dir):
        for fname in sorted(os.listdir(srv_dir)):
            if not fname.endswith(".srv"):
                continue
            name = fname[:-4]
            text = open(os.path.join(srv_dir, fname), encoding="utf-8").read()
            req, _, resp = text.partition("---")
            r = Message(package=package, name=name, kind="srv_request", parent=name)
            parse_fields(package, req, r)
            p = Message(package=package, name=name, kind="srv_response", parent=name)
            parse_fields(package, resp, p)
            messages.append(r)
            messages.append(p)

    act_dir = os.path.join(pkg_dir, "action")
    if os.path.isdir(act_dir):
        for fname in sorted(os.listdir(act_dir)):
            if not fname.endswith(".action"):
                continue
            name = fname[:-7]
            text = open(os.path.join(act_dir, fname), encoding="utf-8").read()
            sections = text.split("---")
            goal, result, feedback = sections[0], sections[1], sections[2]
            for kind, body in (("action_goal", goal), ("action_result", result),
                               ("action_feedback", feedback)):
                m = Message(package=package, name=name, kind=kind, parent=name)
                parse_fields(package, body, m)
                messages.append(m)
    return messages


def load_package_messages_multi(pkg_dirs: List[str], package: str) -> List[Message]:
    """Merge a package's definitions across every root that ships it.

    Roots are consulted in order and the first definition of a given name wins,
    so an overlay root can contribute definitions that the primary root lacks
    without shadowing anything the primary root already provides.
    """
    merged: Dict[Tuple[str, str, str], Message] = {}
    for pkg_dir in pkg_dirs:
        for m in load_package_messages(pkg_dir, package):
            if m.kind.startswith("srv_"):
                group = "srv"
            elif m.kind.startswith("action_"):
                group = "action"
            else:
                group = "msg"
            merged.setdefault((group, m.kind, m.name), m)
    return list(merged.values())


def render_struct(m: Message, indent: str, struct_name: Optional[str] = None,
                  type_name: Optional[str] = None) -> str:
    sname = struct_name or m.name
    lines: List[str] = []
    lines.append(f"{indent}struct {sname}")
    lines.append(f"{indent}{{")
    lines.append(f"{indent}  using ros2_message_tag = void;")
    if type_name:
        lines.append(
            f'{indent}  static constexpr const char * ros2_message_type = "{type_name}";')
    lines.append(f"{indent}  using SharedPtr = std::shared_ptr<{sname}>;")
    lines.append(f"{indent}  using ConstSharedPtr = std::shared_ptr<const {sname}>;")
    lines.append(f"{indent}  using UniquePtr = std::unique_ptr<{sname}>;")
    for c in m.constants:
        lines.append(f"{indent}  static constexpr {c.cpp_type} {c.name} = {c.literal};")
    if m.constants and m.fields:
        lines.append("")
    for f in m.fields:
        if f.kind == "seq":
            ctype = f"std::vector<{f.cpp_type}>"
        elif f.kind == "array":
            ctype = f"std::array<{f.cpp_type}, {f.size}>"
        else:
            ctype = f.cpp_type
        # rosidl declares `using _<field>_type = <type>;` next to every field,
        # and generated code outside these headers reaches for it: nav2's
        # behavior tree nodes spell port types as `Result::_route_type` and
        # friends rather than repeating the message type by hand.
        lines.append(f"{indent}  using _{f.name}_type = {ctype};")
        lines.append(f"{indent}  {ctype} {f.name}{field_default(f)};")
    lines.append("")
    # visit: non-const and const variants.
    for const_qual in ("", " const"):
        lines.append(f"{indent}  template <class Archive>")
        lines.append(f"{indent}  void visit(Archive & ar){const_qual}")
        lines.append(f"{indent}  {{")
        if not m.fields:
            lines.append(f"{indent}    (void)ar;")
        for f in m.fields:
            lines.append(f"{indent}    ar({f.name});")
        lines.append(f"{indent}  }}")
        lines.append("")
    lines.append(f"{indent}  bool operator==(const {sname} & other) const")
    lines.append(f"{indent}  {{")
    if m.fields:
        condition = " &&\n" + " " * (len(indent) + 6)
        joined = condition.join(f"{f.name} == other.{f.name}" for f in m.fields)
        lines.append(f"{indent}    return {joined};")
    else:
        lines.append(f"{indent}    (void)other;")
        lines.append(f"{indent}    return true;")
    lines.append(f"{indent}  }}")
    lines.append(
        f"{indent}  bool operator!=(const {sname} & other) const "
        "{return !(*this == other);}")
    while lines and lines[-1] == "":
        lines.pop()
    lines.append(f"{indent}}};")
    return "\n".join(lines)


def render_message(m: Message, includes: set) -> str:
    pkg = m.package
    for f in m.fields:
        if f.include:
            includes.add(f.include)
        if f.kind in ("seq", "array"):
            includes.add("vector" if f.kind == "seq" else "array")
    if m.kind == "msg":
        snake = camel_to_snake(m.name)
        ns = [f"namespace {pkg}", "namespace msg"]
        body = render_struct(m, "", type_name=f"{pkg}/msg/{m.name}")
        guard = f"{pkg.upper()}_MSG_{m.name.upper()}_HPP_"
    elif m.kind == "srv_request":
        snake = camel_to_snake(m.name)
        ns = [f"namespace {pkg}", "namespace srv"]
        body = (f"struct {m.name}\n{{\n"
                + render_struct(
                    m, "  ", type_name=f"{pkg}/srv/{m.name}_Request"
                ).replace(f"struct {m.name}", "struct Request", 1)
                + "\n};")
        guard = f"{pkg.upper()}_SRV_{m.name.upper()}_HPP_"
    elif m.kind == "srv_response":
        return ""
    elif m.kind == "action_goal":
        snake = camel_to_snake(m.name)
        ns = [f"namespace {pkg}", "namespace action"]
        body = None  # handled by action renderer
        guard = ""
    else:
        return ""

    lines = [f"#ifndef {guard}", f"#define {guard}", ""]
    for inc in sorted(includes):
        lines.append(f"#include <{inc}>" if inc in ("vector", "array") else f'#include "{inc}"')
    lines += ["", "#include <cstdint>", "#include <memory>", "#include <string>", ""]
    lines += macro_guard_lines(m)
    for n in ns:
        lines.append(n + "\n{")
    lines.append("")
    lines.append(body)
    lines.append("")
    for n in reversed(ns):
        lines.append(f"}}  // namespace {n.split()[-1]}")
    lines.append("")
    lines.append(f"#endif  // {guard}")
    lines.append("")
    return "\n".join(lines)


EMITTED: List[str] = []


def emit(output_dir: str, rel_path: str, content: str) -> None:
    full = os.path.join(output_dir, rel_path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    if os.path.exists(full):
        old = open(full, encoding="utf-8").read()
        if old == content:
            if rel_path not in EMITTED:
                EMITTED.append(rel_path)
            return
    with open(full, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(content)
    if rel_path not in EMITTED:
        EMITTED.append(rel_path)


def emit_umbrella(output_dir: str) -> None:
    lines = ["#ifndef ROS2_SHIM_ALL_MESSAGES_HPP",
             "#define ROS2_SHIM_ALL_MESSAGES_HPP", ""]
    for rel in sorted(EMITTED):
        lines.append(f'#include "{rel}"')
    lines += ["", "#endif  // ROS2_SHIM_ALL_MESSAGES_HPP", ""]
    full = os.path.join(output_dir, "ros2_shim", "all_messages.hpp")
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))


PY_SCALAR = {
    "bool": "?", "byte": "B", "char": "B",
    "int8": "b", "uint8": "B", "int16": "h", "uint16": "H",
    "int32": "i", "uint32": "I", "int64": "q", "uint64": "Q",
    "float32": "f", "float64": "d", "wchar": "H",
}


def py_ref(pkg: str, name: str) -> str:
    return f"pymessages.{pkg}.msg.{camel_to_snake(name)}.{name}"


def py_desc(pkg: str, base: str, kind: str, size: int, imports: set):
    """The `_runtime` descriptor for one field.

    `kind` is answered before the scalar table, because `float32[] ranges` is a
    *sequence of* floats and not a float. Looking up the scalar first is what
    made every scalar sequence and every fixed-size scalar array a single value
    on the wire: `LaserScan.ranges` was one float, `OccupancyGrid.data` one
    byte, and `UUID.uuid` - `uint8[16]` - one byte, so a 16-byte identifier
    decoded as a small integer and every field after it came out shifted and
    zero. The C++ side writes the array it declares, so the mismatch was a
    Python-only payload no reader could make sense of.
    """
    if kind == "seq":
        return f"f_seq({py_desc(pkg, base, 'single', 0, imports)[0]})", None
    if kind == "array":
        return f"f_array({py_desc(pkg, base, 'single', 0, imports)[0]}, {size})", None
    if base in PY_SCALAR:
        if base == "bool":
            return "f_bool()", None
        return f"f_scalar('{PY_SCALAR[base]}')", None
    if base == "string":
        return "f_string()", None
    if base == "wstring":
        return "f_wstring()", None
    dep_pkg, dep_name = (base.split("/", 1) if "/" in base else (pkg, base))
    imports.add(py_ref(dep_pkg, dep_name))
    return f"f_msg({dep_name})", dep_name


def py_default(pkg: str, base: str, kind: str, size: int, imports: set) -> str:
    if base == "bool":
        single = "False"
    elif base in ("string", "wstring"):
        single = '""'
    elif base in ("float32", "float64"):
        single = "0.0"
    elif base in PY_SCALAR:
        single = "0"
    else:
        dep_pkg, dep_name = (base.split("/", 1) if "/" in base else (pkg, base))
        imports.add(py_ref(dep_pkg, dep_name))
        single = f"{dep_name}()"
    if kind == "seq":
        return "[]"
    if kind == "array":
        if single in ("0", "0.0", "False", '""'):
            return f"[{single}] * {size}"
        return f"[{single.replace('()', '()')} for _ in range({size})]"
    return single


def render_python_class(m: Message, indent: str, imports: set) -> str:
    lines = [f"{indent}class {m.name}(Msg):"]
    for c in m.constants:
        lines.append(f"{indent}    {c.name} = {c.literal}")
    field_descs = []
    for f in m.fields:
        desc, _ = py_desc(m.package, f.base_type, f.kind, f.size, imports)
        field_descs.append(f"('{f.name}', {desc})")
    if field_descs:
        lines.append(f"{indent}    _fields = [")
        for d in field_descs:
            lines.append(f"{indent}        {d},")
        lines.append(f"{indent}    ]")
    lines.append("")
    lines.append(f"{indent}    def __init__(self):")
    if not m.fields:
        lines.append(f"{indent}        pass")
    for f in m.fields:
        default = py_default(m.package, f.base_type, f.kind, f.size, imports)
        lines.append(f"{indent}        self.{f.name} = {default}")
    return "\n".join(lines)


def generate_python(output_dir: str, packages) -> None:
    _ensure_runtime(output_dir)
    for package, pkg_dirs in packages:
        messages = load_package_messages_multi(pkg_dirs, package)
        for m in messages:
            if m.kind == "msg":
                snake = camel_to_snake(m.name)
                imports: set = set()
                body = render_python_class(m, "", imports)
                _write_py_module(output_dir, package, "msg", snake, body,
                                 imports, m.name, None)
        srv_names = sorted({m.parent for m in messages
                            if m.kind in ("srv_request", "srv_response")})
        for name in srv_names:
            req = next(m for m in messages if m.kind == "srv_request" and m.parent == name)
            resp = next(m for m in messages if m.kind == "srv_response" and m.parent == name)
            imports: set = set()
            body = [f"class {name}:", ""]
            body.append(render_python_class(req, "    ", imports).replace(
                f"class {name}", "class Request", 1))
            body.append("")
            body.append(render_python_class(resp, "    ", imports).replace(
                f"class {name}", "class Response", 1))
            _write_py_module(output_dir, package, "srv", camel_to_snake(name),
                             "\n".join(body), imports, name, "srv")
        act_names = sorted({m.parent for m in messages if m.kind.startswith("action_")})
        for name in act_names:
            imports: set = set()
            body = [f"class {name}:", ""]
            for kind, sub in (("action_goal", "Goal"), ("action_result", "Result"),
                              ("action_feedback", "Feedback")):
                m = next(x for x in messages if x.parent == name and x.kind == kind)
                body.append(render_python_class(m, "    ", imports).replace(
                    f"class {name}", f"class {sub}", 1))
                body.append("")
            # `Impl` after the class, never inside it: a nested class body
            # cannot see the names of the class body around it, so `Goal` is
            # not bound while `Impl` would be built at `class` statement time.
            body.append(f"{name}.Impl = build_action_impl({name})")
            _write_py_module(output_dir, package, "action", camel_to_snake(name),
                             "\n".join(body), imports, name, "action")


def _write_py_module(output_dir, package, subdir, snake, body, imports, class_name,
                     container):
    pkg_dir = os.path.join(output_dir, "pymessages", package, subdir)
    os.makedirs(pkg_dir, exist_ok=True)
    rel_imports = sorted({i for i in imports if i != f"pymessages.{package}.{subdir}.{snake}.{class_name}"})
    if container == "action":
        # An action module also builds its `Impl` namespace on import.
        lines = ["from pymessages._runtime import (",
                 "    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,",
                 "    f_seq, f_string, f_wstring,",
                 ")"]
    else:
        lines = ["from pymessages._runtime import (",
                 "    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,",
                 ")"]
    for ref in rel_imports:
        module, name = ref.rsplit(".", 1)
        lines.append(f"from {module} import {name}")
    lines.append("")
    lines.append(body)
    lines.append("")
    full = os.path.join(pkg_dir, f"{snake}.py")
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))
    _ensure_init(os.path.join(output_dir, "pymessages"))
    _ensure_init(os.path.join(output_dir, "pymessages", package))
    _ensure_init(pkg_dir)


def _ensure_init(path):
    os.makedirs(path, exist_ok=True)
    init = os.path.join(path, "__init__.py")
    if not os.path.exists(init):
        with open(init, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("")


def _ensure_runtime(output_dir: str) -> None:
    template = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "pymessages_runtime.py")
    dest = os.path.join(output_dir, "pymessages", "_runtime.py")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(template, encoding="utf-8") as src:
        content = src.read()
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(content)


def generate_package(output_dir: str, pkg_dirs: List[str], package: str) -> int:
    messages = load_package_messages_multi(pkg_dirs, package)
    # msg
    for m in messages:
        if m.kind == "msg":
            inc: set = set()
            content = render_message(m, inc)
            emit(output_dir, f"{package}/msg/{camel_to_snake(m.name)}.hpp", content)
    # srv (request + response in one header)
    srv_names = sorted({m.parent for m in messages if m.kind in ("srv_request", "srv_response")})
    for name in srv_names:
        req = next(m for m in messages if m.kind == "srv_request" and m.parent == name)
        resp = next(m for m in messages if m.kind == "srv_response" and m.parent == name)
        inc = set()
        body_req = render_struct(req, "    ", "Request", f"{package}/srv/{name}_Request")
        body_resp = render_struct(resp, "    ", "Response", f"{package}/srv/{name}_Response")
        for f in req.fields + resp.fields:
            if f.include:
                inc.add(f.include)
            if f.kind in ("seq", "array"):
                inc.add("vector" if f.kind == "seq" else "array")
        guard = f"{package.upper()}_SRV_{name.upper()}_HPP_"
        lines = [f"#ifndef {guard}", f"#define {guard}", ""]
        for i in sorted(inc):
            lines.append(f'#include "{i}"')
        lines += ["", "#include <cstdint>", "#include <memory>", "#include <string>", "",
                  f"namespace {package}", "{", "namespace srv", "{", "",
                  f"struct {name}", "{",
                  # rosidl names the service itself, and the shim graph labels a
                  # service record with exactly that name (`ros2 service list -t`
                  # prints it upstream). The two halves below already carry
                  # `<pkg>/srv/<Name>_Request` and `..._Response`.
                  f'  static constexpr const char * ros2_service_type = '
                  f'"{package}/srv/{name}";',
                  body_req, "", body_resp, "};", "",
                  "}  // namespace srv", f"}}  // namespace {package}", "",
                  f"#endif  // {guard}", ""]
        emit(output_dir, f"{package}/srv/{camel_to_snake(name)}.hpp", "\n".join(lines))
    # action
    act_names = sorted({m.parent for m in messages if m.kind.startswith("action_")})
    for name in act_names:
        parts = {m.kind: m for m in messages if m.parent == name and m.kind.startswith("action_")}
        inc = set()
        bodies = []
        for kind, sub in (("action_goal", "Goal"), ("action_result", "Result"),
                          ("action_feedback", "Feedback")):
            m = parts[kind]
            for f in m.fields:
                if f.include:
                    inc.add(f.include)
                if f.kind in ("seq", "array"):
                    inc.add("vector" if f.kind == "seq" else "array")
            bodies.append(
                render_struct(m, "    ", sub, f"{package}/action/{name}_{sub}"))
        guard = f"{package.upper()}_ACTION_{name.upper()}_HPP_"
        lines = [f"#ifndef {guard}", f"#define {guard}", ""]
        for i in sorted(inc):
            lines.append(f'#include "{i}"')
        lines += ["", "#include <cstdint>", "#include <memory>", "#include <string>", "",
                  f"namespace {package}", "{", "namespace action", "{", "",
                  f"struct {name}", "{", bodies[0], "", bodies[1], "", bodies[2], "};", "",
                  "}  // namespace action", f"}}  // namespace {package}", "",
                  f"#endif  // {guard}", ""]
        emit(output_dir, f"{package}/action/{camel_to_snake(name)}.hpp", "\n".join(lines))
    return len(messages)


def find_packages(roots: List[str]) -> List[Tuple[str, List[str]]]:
    found: Dict[str, List[str]] = {}
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            if "package.xml" in filenames:
                pkg_name = None
                try:
                    text = open(os.path.join(dirpath, "package.xml"), encoding="utf-8").read()
                    m = re.search(r"<name>([^<]+)</name>", text)
                    if m:
                        pkg_name = m.group(1)
                except OSError:
                    pass
                has_msgs = any(os.path.isdir(os.path.join(dirpath, d))
                               for d in ("msg", "srv", "action"))
                if pkg_name and has_msgs:
                    dirs = found.setdefault(pkg_name, [])
                    if dirpath not in dirs:
                        dirs.append(dirpath)
                dirnames[:] = [d for d in dirnames if d != "build"]
    return sorted(found.items())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", action="append", required=True,
                    help="directory to scan recursively for packages with msg/srv/action")
    ap.add_argument("--output", required=True)
    ap.add_argument("--exclude", action="append", default=[],
                    help="package name to skip")
    ap.add_argument("--python-output", default=None,
                    help="directory to emit pymessages Python package")
    args = ap.parse_args()

    packages = [(p, d) for p, d in find_packages(args.root) if p not in set(args.exclude)]
    total = 0
    for package, pkg_dirs in packages:
        n = generate_package(args.output, pkg_dirs, package)
        total += n
        print(f"  {package:<28} {n:>3} definitions  <- {' + '.join(pkg_dirs)}")
    emit_umbrella(args.output)
    if args.python_output:
        generate_python(args.python_output, packages)
        print(f"generated Python messages into {args.python_output}")
    print(f"generated {total} message definitions from {len(packages)} packages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
