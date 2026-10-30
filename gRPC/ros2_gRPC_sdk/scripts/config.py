#!/usr/bin/env python3
"""Read/write the SDK YAML config file used by the C++ runtime.

Mirrors the C++ resolution in ``ros2_grpc_sdk/common/config.yaml``:

1. ``ROS2_GRPC_SDK_CONFIG_DIR`` environment variable (a directory)
2. ``./config`` if it exists (the folder shipped in the repository)
3. per-user location: ``%APPDATA%/ros2_gRPC_sdk`` (Windows) or
   ``$XDG_CONFIG_HOME/ros2_gRPC_sdk`` / ``~/.config/ros2_gRPC_sdk``

The file is ``ros2_gRPC_sdk.yaml``. YAML is used (not JSON) so the file can
carry comments. Requires PyYAML (``pip install pyyaml``).
"""

from __future__ import annotations

import os
from pathlib import Path

try:
    import yaml
except ImportError as exc:  # pragma: no cover - dependency hint
    raise SystemExit(
        "PyYAML is required: pip install -i "
        "https://mirrors.tuna.tsinghua.edu.cn/pypi/web/simple pyyaml"
    ) from exc

ROOT = Path(__file__).resolve().parent.parent
CONFIG_NAME = "ros2_gRPC_sdk.yaml"

DEFAULTS = {
    "server": {"address": "0.0.0.0:50051", "num_threads": 0},
    "client": {
        "connect_timeout_ms": 3000,
        "rpc_timeout_ms": 5000,
        "use_tls": False,
        "tls_root_cert_pem": "",
        "tls_target_name_override": "",
    },
    "discovery": {
        "ports": [50051],
        "connect_timeout_ms": 150,
        "concurrency": 256,
        "max_hosts_per_subnet": 1024,
        "verify_with_ping": True,
        "ping_timeout_ms": 500,
        "overall_timeout_ms": 3000,
        "include_self": False,
        "include_loopback": False,
        "subnets": [],
        "max_results": 32,
    },
}

_HEADER = (
    "# ros2_gRPC_sdk configuration (YAML - comments are supported).\n"
    "# Written by scripts/config.py; edit the values in place.\n"
)


def config_dir() -> Path:
    override = os.environ.get("ROS2_GRPC_SDK_CONFIG_DIR")
    if override:
        return Path(override)
    local = ROOT / "config"
    if local.is_dir():
        return local
    if os.name == "nt":
        appdata = os.environ.get("APPDATA")
        if appdata:
            return Path(appdata) / "ros2_gRPC_sdk"
        return Path.home() / "ros2_gRPC_sdk"
    xdg = os.environ.get("XDG_CONFIG_HOME")
    base = Path(xdg) if xdg else Path.home() / ".config"
    return base / "ros2_gRPC_sdk"


def config_path(name: str = CONFIG_NAME) -> Path:
    return config_dir() / name


def _merge(base: dict, override: dict) -> dict:
    merged = {k: (dict(v) if isinstance(v, dict) else list(v) if isinstance(v, list) else v)
              for k, v in base.items()}
    for key, value in (override or {}).items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key].update(value)
        else:
            merged[key] = value
    return merged


def load_config(path: str | os.PathLike | None = None) -> dict:
    candidate = Path(path) if path else Path(
        os.environ.get("ROS2_GRPC_SDK_CONFIG", config_path())
    )
    if not candidate.is_file():
        fallback = ROOT / "config" / CONFIG_NAME
        if fallback.is_file():
            candidate = fallback
        else:
            return _merge(DEFAULTS, {})
    data = yaml.safe_load(candidate.read_text(encoding="utf-8")) or {}
    return _merge(DEFAULTS, data)


def save_config(config: dict, path: str | os.PathLike | None = None) -> Path:
    target = Path(path) if path else config_path()
    target.parent.mkdir(parents=True, exist_ok=True)
    body = yaml.safe_dump(
        config, default_flow_style=None, sort_keys=False, allow_unicode=True
    )
    target.write_text(_HEADER + body, encoding="utf-8")
    return target


def server_address(config: dict | None = None) -> str:
    return (config or load_config())["server"]["address"]


def discovery_ports(config: dict | None = None) -> list[int]:
    return list((config or load_config())["discovery"]["ports"])


def discovery_options(config: dict | None = None) -> dict:
    return dict((config or load_config())["discovery"])


if __name__ == "__main__":
    loaded = load_config()
    print(f"config dir : {config_dir()}")
    print(f"config file: {config_path()}")
    print(yaml.safe_dump(loaded, default_flow_style=None, sort_keys=False))
