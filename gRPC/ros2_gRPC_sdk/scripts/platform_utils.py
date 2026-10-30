#!/usr/bin/env python3
"""Platform detection helpers shared by the build and launcher scripts.

A "platform key" is a low-case string such as::

    windows-x86_64            # Windows, MinGW/UCRT64 (default)
    windows-x86_64-msvc       # Windows, MSVC toolchain
    linux-x86_64
    linux-aarch64
    darwin-arm64

The key is used both as the per-platform build directory name
(``build/<platform-key>``) and as the entry name in ``build/platforms.yaml``.
The manifest records what has been built so that the launcher can pick the
right artifacts for the machine it runs on.
"""

from __future__ import annotations

import os
import platform as _platform
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
BUILD_ROOT = ROOT / "build"
MANIFEST_NAME = "platforms.yaml"

_ARCH_ALIASES = {
    "amd64": "x86_64",
    "x86_64": "x86_64",
    "x64": "x86_64",
    "aarch64": "aarch64",
    "arm64": "aarch64",
    "i386": "x86",
    "i486": "x86",
    "i586": "x86",
    "i686": "x86",
    "x86": "x86",
    "armv7l": "armv7l",
}


def detect_os() -> str:
    if os.name == "nt":
        return "windows"
    system = _platform.system().lower()
    if system.startswith("darwin"):
        return "darwin"
    return system or "unknown"


def detect_arch() -> str:
    machine = _platform.machine().lower()
    return _ARCH_ALIASES.get(machine, machine or "unknown")


def host_key() -> str:
    """Platform key for the machine the script is running on."""
    return f"{detect_os()}-{detect_arch()}"


def exe_suffix(platform_key: str | None = None) -> str:
    key = platform_key or host_key()
    return ".exe" if key.startswith("windows") else ""


def build_dir_for(platform_key: str, build_root: Path = BUILD_ROOT) -> Path:
    return Path(build_root) / platform_key


def load_manifest(build_root: Path = BUILD_ROOT) -> dict:
    path = Path(build_root) / MANIFEST_NAME
    try:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"platforms": {}}
    if not isinstance(data, dict):
        return {"platforms": {}}
    data.setdefault("platforms", {})
    return data


def save_manifest(data: dict, build_root: Path = BUILD_ROOT) -> None:
    root = Path(build_root)
    root.mkdir(parents=True, exist_ok=True)
    (root / MANIFEST_NAME).write_text(
        yaml.safe_dump(data, default_flow_style=None, sort_keys=False),
        encoding="utf-8",
    )


def record_platform(
    platform_key: str,
    *,
    build_dir: Path,
    platform_os: str,
    arch: str,
    toolchain: str,
    generator: str,
    build_root: Path = BUILD_ROOT,
) -> None:
    data = load_manifest(build_root)
    try:
        rel = str(Path(build_dir).resolve().relative_to(ROOT.resolve()))
    except ValueError:
        rel = str(Path(build_dir))
    data["platforms"][platform_key] = {
        "os": platform_os,
        "arch": arch,
        "toolchain": toolchain,
        "generator": generator,
        "build_dir": rel.replace("\\", "/"),
        "exe_suffix": ".exe" if platform_os == "windows" else "",
    }
    save_manifest(data, build_root)


def resolve_build_dir(
    platform_key: str | None = None, build_root: Path = BUILD_ROOT
) -> Path:
    """Pick the build directory to launch from.

    Priority: exact manifest match for ``platform_key`` / host key, then any
    manifest entry with the same OS + arch, then the conventional
    ``build/<key>`` directory, then a legacy flat ``build/`` directory.
    """
    key = platform_key or host_key()
    data = load_manifest(build_root)
    platforms = data.get("platforms", {})

    if key in platforms:
        return ROOT / platforms[key]["build_dir"]

    os_name, _, arch = key.partition("-")
    for entry in platforms.values():
        if entry.get("os") == os_name and entry.get("arch") == arch:
            return ROOT / entry["build_dir"]

    candidate = build_dir_for(key, build_root)
    if candidate.exists():
        return candidate
    if Path(build_root).exists():
        return Path(build_root)
    return candidate


def list_built(build_root: Path = BUILD_ROOT) -> list[str]:
    return sorted(load_manifest(build_root).get("platforms", {}).keys())
