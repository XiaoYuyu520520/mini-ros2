#!/usr/bin/env python3
"""Configure and build ros2_gRPC_sdk for one or more platforms.

Everything is driven from Python with direct subprocess calls (no PowerShell or
cmd batch files). Each platform is built into its own directory,
``build/<platform-key>``, and recorded in ``build/platforms.yaml`` so the
launcher can pick the right binaries for the current machine. A tree whose
recorded configure arguments no longer match the command line is rebuilt
from scratch rather than reused; see the note above ``build_one``.

Examples
--------
    python scripts/build.py                  # host platform (auto)
    python scripts/build.py --list           # show detected platforms
    python scripts/build.py --platform all   # every toolchain available here
    python scripts/build.py --platform windows-x86_64-msvc
    python scripts/build.py --clean --test
    python scripts/build.py --with-ros2
    python scripts/build.py --toolchain cmake/linux-toolchain.cmake --platform linux-aarch64
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
# The "six big cores, never every core" policy and the MSYS2 root are the
# workspace's, not this script's: tools/_cores.py owns the topology detection,
# the P-core split, the affinity pin and the BIG6_* overrides, while
# tools/_toolchain.py owns the prefix. This tree is part of that workspace, so
# it imports them rather than keeping a second copy that can drift - which is
# what nav2/ros2_shim/build.py, bridge/scripts/build_bridge.py and
# tools/run_ctest.py already do.
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))

from platform_utils import (  # noqa: E402
    ROOT,
    detect_arch,
    detect_os,
    host_key,
    record_platform,
)
from _cores import DEFAULT_JOBS, pin  # noqa: E402
from _toolchain import default_msys2  # noqa: E402

EXE_SUFFIX = ".exe" if os.name == "nt" else ""


def log(message: str) -> None:
    print(f"[build] {message}", flush=True)


def run(cmd: list[str], env: dict | None = None, cwd: Path | None = None) -> None:
    log(" ".join(str(part) for part in cmd))
    subprocess.run([str(part) for part in cmd], check=True, env=env, cwd=cwd)


def resolve_tool(name: str, search_dirs: list[Path]) -> Path | None:
    filename = name + EXE_SUFFIX
    for directory in search_dirs:
        candidate = Path(directory) / filename
        if candidate.is_file():
            return candidate
    found = shutil.which(name)
    return Path(found) if found else None


# ---------------------------------------------------------------------------
# Toolchain discovery
# ---------------------------------------------------------------------------


def _windows_msvc_arch(arch: str) -> str:
    return {"x86_64": "x64", "aarch64": "ARM64", "x86": "Win32"}.get(arch, "x64")


def detect_toolchains(args: argparse.Namespace) -> dict[str, dict]:
    """Return {platform_key: descriptor} for toolchains usable on this host."""
    descriptors: dict[str, dict] = {}
    arch = detect_arch()
    env = dict(os.environ)

    if detect_os() == "windows":
        ucrt = Path(args.msys_root) / "ucrt64"
        bin_dir = ucrt / "bin"
        tools = {n: resolve_tool(n, [bin_dir]) for n in ("cmake", "ninja", "g++")}
        if all(tools.values()):
            gnu_env = dict(env)
            gnu_env["PATH"] = os.pathsep.join([str(bin_dir), gnu_env.get("PATH", "")])
            descriptors[f"windows-{arch}"] = {
                "key": f"windows-{arch}",
                "os": "windows",
                "arch": arch,
                "toolchain": "gnu",
                "generator": "Ninja",
                "multi_config": False,
                "env": gnu_env,
                "bin_dir": bin_dir,
                "cache": [
                    f"-DCMAKE_CXX_COMPILER={tools['g++']}".replace("\\", "/"),
                    f"-DCMAKE_PREFIX_PATH={ucrt}".replace("\\", "/"),
                ],
            }

        vcpkg_root = args.vcpkg_root or os.environ.get("VCPKG_ROOT")
        if not vcpkg_root:
            for candidate in ("C:/vcpkg", "C:/dev/vcpkg", "C:/tools/vcpkg"):
                if (Path(candidate) / "scripts" / "buildsystems" / "vcpkg.cmake").is_file():
                    vcpkg_root = candidate
                    break
        if vcpkg_root:
            toolchain_file = (
                Path(vcpkg_root) / "scripts" / "buildsystems" / "vcpkg.cmake"
            )
            if toolchain_file.is_file():
                descriptors[f"windows-{arch}-msvc"] = {
                    "key": f"windows-{arch}-msvc",
                    "os": "windows",
                    "arch": arch,
                    "toolchain": "msvc",
                    "generator": args.vs_generator,
                    "multi_config": True,
                    "vs_arch": _windows_msvc_arch(arch),
                    "env": env,
                    "bin_dir": None,
                    "cache": [
                        f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}".replace("\\", "/")
                    ],
                }
    else:
        descriptors[f"{detect_os()}-{arch}"] = {
            "key": f"{detect_os()}-{arch}",
            "os": detect_os(),
            "arch": arch,
            "toolchain": "native",
            "generator": "Ninja",
            "multi_config": False,
            "env": env,
            "bin_dir": None,
            "cache": [],
        }
        if args.toolchain:
            descriptors[f"{detect_os()}-{arch}"]["cache"].append(
                f"-DCMAKE_TOOLCHAIN_FILE={Path(args.toolchain).resolve()}".replace(
                    "\\", "/"
                )
            )

    return descriptors


def select_toolchains(
    descriptors: dict[str, dict], requested: str
) -> list[dict]:
    if requested == "auto":
        # Prefer the native GNU/native toolchain for the host.
        key = host_key()
        if key in descriptors:
            return [descriptors[key]]
        return [next(iter(descriptors.values()))]

    if requested == "all":
        return list(descriptors.values())

    selected = []
    for key in (part.strip() for part in requested.split(",")):
        if not key:
            continue
        if key not in descriptors:
            raise SystemExit(
                f"platform '{key}' is not available on this host. "
                f"available: {', '.join(sorted(descriptors)) or 'none'}"
            )
        selected.append(descriptors[key])
    if not selected:
        raise SystemExit("no platform selected")
    return selected


# ---------------------------------------------------------------------------
# Build
#
# A CMake cache remembers every option it was ever configured with, and
# `-DROS2_GRPC_SDK_WITH_ROS2=ON` is the one that hurts: it stays ON for later
# runs that no longer pass the flag, so those runs try `find_package(rclcpp)`
# in a shell that never sourced ROS and fail at configure. That is a red build
# caused by a flag nobody asked for, and it happened for real: the remote half
# of `tools/check_cross_platform.py` went red against a `build/linux-x86_64`
# left behind by an earlier `--with-ros2` run, and green again after
# `rm -rf build`.
#
# Two things keep it from coming back, and they cover different halves of the
# problem. `ROS2_GRPC_SDK_WITH_ROS2` is passed explicitly (`=ON` or `=OFF`) on
# every configure, so the cache cannot hold a value the current command line
# did not ask for - that is the half that heals a tree somebody configured by
# hand. And the arguments that shape the configure are written next to the
# cache, so a run that disagrees with the tree on disk rebuilds it instead of
# reusing it.
# ---------------------------------------------------------------------------

FINGERPRINT_NAME = ".configure-fingerprint.json"


def configure_fingerprint(tc: dict, args: argparse.Namespace) -> dict:
    """The configure-shaping inputs, as a value that can be compared."""
    return {
        "version": 1,
        "platform": tc["key"],
        "toolchain": tc["toolchain"],
        "generator": tc["generator"],
        "build_type": args.config,
        "with_ros2": bool(args.with_ros2),
        "examples": not args.no_examples,
        "tests": not args.no_tests,
        "cache": list(tc["cache"]),
    }


def fingerprint_path(build_dir: Path) -> Path:
    return build_dir / FINGERPRINT_NAME


def read_fingerprint(build_dir: Path) -> dict | None:
    try:
        return json.loads(fingerprint_path(build_dir).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def describe_fingerprint_change(recorded: dict | None, current: dict) -> str:
    if recorded is None:
        return "no fingerprint was written for it"
    changed = [
        f"{name}: {recorded.get(name)!r} -> {current[name]!r}"
        for name in sorted(current)
        if recorded.get(name) != current[name]
    ]
    return ", ".join(changed) if changed else "it differs"


def build_one(tc: dict, args: argparse.Namespace, build_root: Path) -> None:
    key = tc["key"]
    build_dir = (build_root / key).resolve()

    cmake = resolve_tool("cmake", [tc["bin_dir"]] if tc["bin_dir"] else [])
    cmake = cmake or Path("cmake")
    ctest = resolve_tool("ctest", [tc["bin_dir"]] if tc["bin_dir"] else [])
    ctest = ctest or Path("ctest")

    # `--clean` is the explicit way to start over; a fingerprint that does not
    # match the tree on disk is the implicit one, and it is not optional - the
    # cache it would otherwise keep belongs to a different build. A tree with no
    # fingerprint at all counts as a mismatch, which is what reconfigures the
    # poisoned tree above on its next build.
    fingerprint = configure_fingerprint(tc, args)
    if args.clean and build_dir.exists():
        log(f"[{key}] removing {build_dir} (--clean)")
        shutil.rmtree(build_dir)
    elif build_dir.exists():
        recorded = read_fingerprint(build_dir)
        if recorded != fingerprint:
            log(f"[{key}] removing {build_dir} (configure fingerprint changed: "
                f"{describe_fingerprint_change(recorded, fingerprint)})")
            shutil.rmtree(build_dir)

    configure = [
        cmake,
        "-S",
        ROOT,
        "-B",
        build_dir,
        "-G",
        tc["generator"],
    ]
    if tc.get("vs_arch"):
        configure += ["-A", tc["vs_arch"]]
    configure += [
        f"-DCMAKE_BUILD_TYPE={args.config}",
        f"-DROS2_GRPC_SDK_BUILD_EXAMPLES={'OFF' if args.no_examples else 'ON'}",
        f"-DROS2_GRPC_SDK_BUILD_TESTS={'OFF' if args.no_tests else 'ON'}",
        # Written out both ways rather than only when set: a cached ON survives
        # every later configure that stays silent about it.
        f"-DROS2_GRPC_SDK_WITH_ROS2={'ON' if args.with_ros2 else 'OFF'}",
    ]
    configure += tc["cache"]

    env = tc["env"]
    log(f"[{key}] building ({tc['toolchain']}, {tc['generator']})")
    run(configure, env=env)
    fingerprint_path(build_dir).write_text(
        json.dumps(fingerprint, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    # 只用 6 个大核：-j 6 + 亲和性钉住 P-core（策略与实测见 tools/_cores.py）
    log(pin())
    build_cmd = [cmake, "--build", build_dir, "--parallel", str(args.jobs)]
    if tc["multi_config"]:
        build_cmd += ["--config", args.config]
    run(build_cmd, env=env)

    if args.test and not args.no_tests:
        test_cmd = [ctest, "--test-dir", build_dir, "--output-on-failure"]
        if tc["multi_config"]:
            test_cmd += ["-C", args.config]
        run(test_cmd, env=env)

    record_platform(
        key,
        build_dir=build_dir,
        platform_os=tc["os"],
        arch=tc["arch"],
        toolchain=tc["toolchain"],
        generator=tc["generator"],
        build_root=build_root,
    )
    log(f"[{key}] done -> {build_dir}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build ros2_gRPC_sdk")
    parser.add_argument(
        "--platform",
        default="auto",
        help="'auto', 'all', or comma separated platform keys "
        "(e.g. windows-x86_64,windows-x86_64-msvc)",
    )
    parser.add_argument("--build-root", default="build")
    parser.add_argument(
        "--jobs",
        type=int,
        default=DEFAULT_JOBS,
        help="parallel build jobs (default %(default)s: the 6 big cores, never every core)",
    )
    parser.add_argument(
        "--config", default="Release", choices=["Debug", "Release", "RelWithDebInfo"]
    )
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--with-ros2", action="store_true")
    parser.add_argument("--no-examples", action="store_true")
    parser.add_argument("--no-tests", action="store_true")
    parser.add_argument("--test", action="store_true", help="run ctest after building")
    parser.add_argument("--list", action="store_true", help="list detected platforms")
    parser.add_argument(
        "--msys-root",
        default=str(default_msys2()),
        help="MSYS2 installation root (Windows only)",
    )
    parser.add_argument(
        "--vcpkg-root",
        default=None,
        help="vcpkg root used for the MSVC toolchain (Windows)",
    )
    parser.add_argument(
        "--vs-generator",
        default="Visual Studio 17 2022",
        help="CMake generator for the MSVC toolchain",
    )
    parser.add_argument(
        "--toolchain",
        default=None,
        help="CMake toolchain file, used for cross builds on Linux/macOS",
    )
    args = parser.parse_args()

    build_root = (ROOT / args.build_root).resolve()
    descriptors = detect_toolchains(args)

    # Allow an explicit cross build when a CMake toolchain file is supplied.
    if args.toolchain and args.platform not in ("auto", "all", "") and "," not in args.platform:
        key = args.platform.strip()
        if key not in descriptors:
            os_name, _, rest = key.partition("-")
            arch = rest.split("-")[0] if rest else detect_arch()
            descriptors[key] = {
                "key": key,
                "os": os_name,
                "arch": arch,
                "toolchain": "cross",
                "generator": args.vs_generator if os_name == "windows" else "Ninja",
                "multi_config": False,
                "env": dict(os.environ),
                "bin_dir": None,
                "cache": [
                    f"-DCMAKE_TOOLCHAIN_FILE={Path(args.toolchain).resolve()}".replace(
                        "\\", "/"
                    )
                ],
            }

    if args.list:
        log(f"host: {host_key()}")
        if not descriptors:
            log("  (no usable toolchain detected)")
        for key in sorted(descriptors):
            tc = descriptors[key]
            log(f"  {key:<28} toolchain={tc['toolchain']:<7} generator={tc['generator']}")
        return 0

    if not descriptors:
        raise SystemExit("no usable toolchain found on this host")

    selected = select_toolchains(descriptors, args.platform)
    log(f"host={host_key()} building {len(selected)} platform(s): "
        f"{', '.join(tc['key'] for tc in selected)}")

    for tc in selected:
        build_one(tc, args, build_root)

    log(f"build finished: {build_root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
