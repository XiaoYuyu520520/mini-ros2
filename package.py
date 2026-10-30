#!/usr/bin/env python3
"""Bundle a built tree into one release archive.

The archive holds what actually *runs*, not everything the build produced:

  * the nav2 servers and the tools nav2 needs (`apps/` becomes the binaries;
    the `test_*` / `*_probe` / `bench_*` programs are development scaffolding
    and would roughly double the archive for nothing);
  * the bridge daemon and `ros2.exe`;
  * the real-ROS2 agent (`ros2_grpc_sdk_agent`) for the peer machine;
  * the config the stack reads at startup, including the few files it pulls
    out of `nav2/nav2_extracted/` (96 MB of source, of which only the params
    and the behavior trees - 58 KB - are read);
  * the start scripts, the environment files they call, and the Python side.

The runtime DLLs are already copied next to each `.exe` by the build (see
`bridge/windows/cmake/bundle_runtime.cmake`), so a Windows archive needs no
MSYS2 on the target machine.

The release names are fixed, so a tag maps to one artefact per platform:

    mini-ros2-windows-x86_64-<version>.zip
    mini-ros2-linux-x86_64-<version>.tar.gz

    python package.py                          # -> dist/mini-ros2-<platform>-<version>.<ext>
    python package.py --platform linux         # build the other platform's tree
    python package.py --version 0.2.0 --out x.zip
    python package.py --list                   # show what would go in, write nothing
"""
from __future__ import annotations

import argparse
import datetime as _dt
import os
import sys
import tarfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent
DIST = REPO / "dist"

#: The name the project is released under.
PROJECT = "mini-ros2"

#: Per platform: the binary suffix, the SDK build key and the archive format.
#:
#: `label` is what the file name says. The ARM64 build is made on
#: Ubuntu 22.04, so its packages say so: a tarball built there needs glibc
#: 2.35, and a user on Ubuntu 22.04 should be able to tell that from one
#: built on a newer distribution.
PLATFORMS = {
    "windows": {"exe": ".exe", "sdk": "windows-x86_64", "archive": "zip",
                "label": "windows-x86_64"},
    "linux": {"exe": "", "sdk": "linux-x86_64", "archive": "tar.gz",
              "label": "linux-x86_64"},
    "linux-arm64": {"exe": "", "sdk": "linux-aarch64", "archive": "tar.gz",
                    "label": "ubuntu22-arm64"},
}

# ---------------------------------------------------------------------------
# What runs. Keep this list next to `apps/` - a server added there and not here
# is a binary the archive silently lacks.
# ---------------------------------------------------------------------------

#: `nav2/ros2_shim/apps/*.cpp` -> the exe next to its build directory.
NAV2_SERVERS = (
    "amcl_bridge_node",
    "behavior_server",
    "bt_navigator",
    "bt_smoke",
    "collision_monitor",
    "controller_server",
    "costmap_filter_info_server",
    "docking_server",
    "lifecycle_manager",
    "map_saver",
    "map_server",
    "nav2_bridge_demo",
    "nav2_goal",
    "nav2_waypoints",
    "planner_server",
    "route_server",
    "smoother_server",
    "vector_object_server",
    "velocity_smoother",
    "waypoint_follower",
)

#: Live diagnostics that are worth shipping: they are how a field install is
#: debugged without rebuilding (the parameter stream, the map, TF).
NAV2_TOOLS = (
    "map_dump",
    "map_saver_cli",
    "parameter_events_probe",
    "rosout_probe",
    "tf_probe",
    "tf_listener_probe",
)

#: Things a user runs by hand on the Windows side.
BRIDGE_BINARIES = (
    "ros2",
    "ros2_bridge_daemon",
    # The visualiser's data plane: a shim node that transforms every message
    # into the view's fixed frame and hands it to viz.exe over stdio.
    "viz_feed",
)

#: The `.bat` / `.ps1` entry points. Missing one is a broken archive, so they
#: are listed rather than globbed.
BRIDGE_SCRIPTS = (
    "bridge_env.bat",
    "ros2.bat",
    "ros2.ps1",
    "ros2_shell.bat",
    "ros2_bridge.bat",
    "wait_bridge_ready.bat",
    "nav2_stack.bat",
    "nav2_demo.bat",
    "nav2_safety.bat",
    "nav2_docking_route.bat",
    "slam_demo.bat",
    "amcl_demo.bat",
    # `rviz2` by name: the visualiser replaces rviz2 on this platform.
    "rviz2.bat",
)

#: The Linux entry points, which are the same four as the Windows ones.
LINUX_SCRIPTS = (
    "bridge_env.sh",
    "ros2.sh",
    "ros2_bridge.sh",
    "wait_bridge_ready.sh",
)

#: The task's own entry points, per platform.
START_WINDOWS = (
    "加载模拟ROS环境变量.ps1",
    "Windows被连接服务进程.ps1",
)
START_LINUX = (
    "connector.sh",
    "加载模拟ROS环境变量.sh",
)

#: What the stack reads at startup. `nav2_extracted` is 96 MB of source of
#: which these two directories (58 KB) are the only part a run touches:
#: `nav2_bringup/params` is the params file, and
#: `nav2_bt_navigator/behavior_trees` is what `bt_search_directories` walks.
NAV2_EXTRACTED_SUBSET = (
    "nav2_bringup/params",
    "nav2_bt_navigator/behavior_trees",
)

#: The simulator and the relay, run on the peer machine.
UBUNTU_PACKAGES = (
    "bridge/ubuntu/ros2_bridge_sim",
    "bridge/ubuntu/ros2_link",
)

#: Cross-machine helpers, and what the connector scripts call.
SCRIPTS_DIR = "bridge/scripts"

#: The documents that travel with the binaries.
DOCS = (
    "README.md",
    "GETTING_STARTED.md",
    "BUILD.md",
)


def _warn(message: str) -> None:
    print(f"[package] warning: {message}", file=sys.stderr)


def _iter_tree(directory: Path):
    """Every file under `directory`, skipping the things that never belong in
    a distributed archive."""
    skip_dirs = {"__pycache__", ".pytest_cache", ".git"}
    skip_suffixes = {".pyc", ".pyo"}
    for root, dirs, files in os.walk(directory):
        dirs[:] = sorted(d for d in dirs if d not in skip_dirs)
        for name in sorted(files):
            if os.path.splitext(name)[1] in skip_suffixes:
                continue
            yield Path(root) / name


def collect(platform: str, args: argparse.Namespace) -> list[tuple[Path, str]]:
    """`[(absolute source, path inside the archive)]` for everything to ship."""
    entries: list[tuple[Path, str]] = []
    missing: list[str] = []
    exe = PLATFORMS[platform]["exe"]

    def add_file(src: Path, arc: str) -> None:
        if src.is_file():
            entries.append((src, arc))
        else:
            missing.append(str(src))

    def add_tree(src: Path, arc_root: str) -> None:
        if not src.is_dir():
            missing.append(str(src))
            return
        for path in _iter_tree(src):
            entries.append((path, f"{arc_root}/{path.relative_to(src).as_posix()}"))

    shim_build = REPO / "nav2" / "ros2_shim" / "build"
    bridge_build = REPO / "bridge" / "windows" / "build"
    sdk_build = REPO / "gRPC" / "ros2_gRPC_sdk" / "build" / PLATFORMS[platform]["sdk"]

    # The archive keeps the *repository layout*: every path a start script
    # builds at run time (bridge/windows/build, nav2/ros2_shim/build,
    # nav2/nav2_extracted/navigation2-1.5.1) has to be the same inside the
    # archive, or the unpacked tree is a set of binaries that cannot find each
    # other. Flattening any of these breaks the package.

    # -- binaries ------------------------------------------------------------
    for name in NAV2_SERVERS + NAV2_TOOLS:
        add_file(shim_build / (name + exe), "nav2/ros2_shim/build/" + name + exe)
    if args.with_tests:
        for path in _iter_tree(shim_build):
            if path.name.startswith(("test_", "bench_")) and path.suffix == exe:
                entries.append((path, "nav2/ros2_shim/build/" + path.name))

    for name in BRIDGE_BINARIES:
        add_file(bridge_build / (name + exe), "bridge/windows/build/" + name + exe)

    # Runtime DLLs the build already dropped next to the bridge exes. The nav2
    # build links its own statically; the bridge does not - and only Windows
    # has them.
    if platform == "windows":
        for path in _iter_tree(bridge_build):
            if path.suffix.lower() == ".dll":
                entries.append((path, "bridge/windows/build/" + path.name))

    if not args.no_ros2_agent:
        add_file(sdk_build / ("ros2_grpc_sdk_agent" + exe),
                 "gRPC/ros2_gRPC_sdk/build/" + PLATFORMS[platform]["sdk"]
                 + "/ros2_grpc_sdk_agent" + exe)

    # -- start scripts and their environment --------------------------------
    start_dir = REPO / "start"
    for name in (START_WINDOWS if platform == "windows" else START_LINUX):
        add_file(start_dir / name, "start/" + name)

    if platform == "windows":
        windows_dir = REPO / "bridge" / "windows"
        for name in BRIDGE_SCRIPTS:
            add_file(windows_dir / name, "bridge/windows/" + name)
    else:
        linux_dir = REPO / "bridge" / "linux"
        for name in LINUX_SCRIPTS:
            add_file(linux_dir / name, "bridge/linux/" + name)

    # -- the visualiser ------------------------------------------------------
    # viz.exe is the Go/Fyne front end; it starts viz_feed.exe (above) as a
    # child process and talks to it over stdio. Its config sits next to it, so
    # the archive's layout is what the GUI expects.
    viz_dir = REPO / "viz"
    if platform == "windows":
        add_file(viz_dir / "viz.exe", "viz/viz.exe")
    add_file(viz_dir / "config.json", "viz/config.json")
    add_file(viz_dir / "README.md", "viz/README.md")

    # -- config the stack reads --------------------------------------------
    add_tree(REPO / "nav2" / "ros2_shim" / "config", "nav2/ros2_shim/config")
    for rel in NAV2_EXTRACTED_SUBSET:
        add_tree(REPO / "nav2" / "nav2_extracted" / "navigation2-1.5.1" / rel,
                 "nav2/nav2_extracted/navigation2-1.5.1/" + rel)

    # -- Python side (simulator + relay) ------------------------------------
    for rel in UBUNTU_PACKAGES:
        add_tree(REPO / rel, rel)

    # -- cross-machine helpers ---------------------------------------------
    scripts = REPO / SCRIPTS_DIR
    if scripts.is_dir():
        for path in _iter_tree(scripts):
            if path.suffix == ".py" or path.name.endswith(".sh"):
                entries.append((path, SCRIPTS_DIR + "/" + path.name))

    # -- the docs a field install actually needs ---------------------------
    for name in DOCS:
        add_file(REPO / name, name)

    if missing:
        for item in missing:
            _warn("missing, not packaged: " + item)
    return entries


def write_zip(out: Path, entries: list[tuple[Path, str]], stamp) -> int:
    written = 0
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        for src, arc in entries:
            info = zipfile.ZipInfo(arc, date_time=stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            # Executables and scripts keep their permission bits so an archive
            # that is unpacked on Linux still runs.
            executable = src.suffix.lower() in (".exe", ".sh") or os.access(src, os.X_OK)
            info.external_attr = ((0o755 if executable else 0o644) & 0xFFFF) << 16
            with src.open("rb") as handle:
                archive.writestr(info, handle.read())
            written += 1
    return written


def write_tar(out: Path, entries: list[tuple[Path, str]], stamp) -> int:
    written = 0
    # A fixed mtime makes the archive reproducible: the same build always
    # produces the same bytes, so two archives can be compared with `cmp`.
    epoch = int(stamp[0] * 31556952 + stamp[1] * 2629746 + stamp[2] * 86400)
    with tarfile.open(out, "w:gz", compresslevel=9) as archive:
        for src, arc in entries:
            info = tarfile.TarInfo(arc)
            info.size = src.stat().st_size
            info.mtime = epoch
            info.mode = 0o755 if (src.suffix.lower() == ".sh" or os.access(src, os.X_OK)) else 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            with src.open("rb") as handle:
                archive.addfile(info, handle)
            written += 1
    return written


def _clean_version(version: str) -> str:
    """A version string that is safe inside a file name.

    CI passes `github.ref_name`, which is `v0.2.0` for a tag and something
    like `123/merge` for a pull request. The leading `v` is dropped (the file
    name already says what it is) and every character that is not file-name
    safe becomes `-`, so `123/merge` turns into `123-merge` instead of
    creating a directory.
    """
    version = version.strip()
    if len(version) > 1 and version[0] in ("v", "V") and version[1].isdigit():
        version = version[1:]
    cleaned = "".join(ch if (ch.isalnum() or ch in "._-") else "-" for ch in version)
    return cleaned or _dt.date.today().strftime("%Y%m%d")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--platform", choices=sorted(PLATFORMS), default=None,
                        help="which platform's tree to package (default: this one)")
    parser.add_argument("--version", default=None,
                        help="version in the file name (default: today's date)")
    parser.add_argument("--out", default=None, help="output archive")
    parser.add_argument("--with-tests", action="store_true",
                        help="also ship the test_/bench_ binaries")
    parser.add_argument("--no-ros2-agent", action="store_true",
                        help="skip the peer-side ros2_grpc_sdk_agent")
    parser.add_argument("--list", action="store_true",
                        help="print the file list and the size, write nothing")
    parser.add_argument("--force", action="store_true",
                        help="overwrite an existing output archive")
    args = parser.parse_args(argv)

    platform = args.platform or ("windows" if os.name == "nt" else "linux")
    version = _clean_version(args.version or os.environ.get("CPU_VERSION")
                             or _dt.date.today().strftime("%Y%m%d"))
    suffix = PLATFORMS[platform]["archive"]
    default_name = f"{PROJECT}-{PLATFORMS[platform]['label']}-{version}.{suffix}"

    entries = collect(platform, args)
    if not entries:
        print("[package] nothing to package - build first "
              "(python nav2/ros2_shim/build.py && python bridge/scripts/build_bridge.py)",
              file=sys.stderr)
        return 1

    total = sum(src.stat().st_size for src, _ in entries)

    if args.list:
        for src, arc in entries:
            print(f"{src.stat().st_size:>12,}  {arc}")
        print(f"\n{len(entries)} file(s), {total / 1e6:.1f} MB uncompressed")
        return 0

    out = Path(args.out) if args.out else (DIST / default_name)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists() and not args.force:
        print(f"[package] {out} exists; pass --force to overwrite", file=sys.stderr)
        return 1

    stamp = (_dt.date.today().timetuple()[:3] + (0, 0, 0))
    if suffix == "zip":
        written = write_zip(out, entries, stamp)
    else:
        written = write_tar(out, entries, stamp)

    size = out.stat().st_size
    print(f"[package] {written} file(s), {total / 1e6:.1f} MB -> {size / 1e6:.1f} MB")
    print(f"[package] wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
