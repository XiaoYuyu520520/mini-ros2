#!/usr/bin/env python3
"""Configure and build the shim + shared-memory transport.

The Python twin of ``build.ps1``. Same two steps (configure with Ninja, then
build), same policy on cores -- but no PowerShell, for one concrete reason:
PowerShell does not expand a variable that sits right after ``=`` inside a bare
word, so ``-DCMAKE_BUILD_TYPE=$BuildType`` writes the *literal* string
``$BuildType`` into the cmake cache, ``rules.ninja`` comes out with
``rule CXX_SCAN__xxx_$BuildType``, and ninja dies with "expected newline, got
lexing error". ``build.ps1`` works around it with quotes and says so; here there
is nothing to work around.

    python nav2/ros2_shim/build.py                 # configure + build everything
    python nav2/ros2_shim/build.py controller_server behavior_server
    python nav2/ros2_shim/build.py --reconfigure   # re-run cmake first (default)
    python nav2/ros2_shim/build.py --no-configure  # build only

工具链：Windows 上用 MSYS2 UCRT64（``--msys2`` / ``CPU_MSYS2_ROOT`` 可换），
其它平台用 ``PATH`` 上的 cmake / ninja / C++ 编译器——都在 ``tools/_toolchain.py`` 里
解析，这个脚本不再自己拼 ``.exe`` 路径。Linux 上就是：

    python3 nav2/ros2_shim/build.py

只用 6 个大核编译：本机是 Intel Core Ultra 5 245K（Arrow Lake，无超线程），共 14 个
逻辑核 = 6 个 P-core + 8 个 E-core。要求是"编译的时候调用 6 个大核心，不要所有核心
一起"，所以这里做两件事：任务数固定 6，并且把构建进程**钉在 P-core 上**——Windows
的子进程继承父进程的亲和性掩码，cmake / ninja / g++ 因此全都留在大核上。

规则本身在 ``tools/_cores.py``，覆盖方式与 ``bridge/scripts/build_bridge.py`` 一致：
``--jobs N``、``BIG6_CORES=0,1,10,11,12,13``、``BIG6_NO_PIN=1``。
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD_DIR = HERE / "build"

# Both questions this script used to answer locally -- where the tools are and
# which cores to use -- are answered once, for both platforms, next to it:
# tools/_toolchain.py (MSYS2 UCRT64 on Windows, the system compiler elsewhere)
# and tools/_cores.py (the six big cores).
sys.path.insert(0, str(HERE.parents[1] / "tools"))
from _cores import DEFAULT_JOBS, pin  # noqa: E402
from _toolchain import build_env, default_msys2, find_tools  # noqa: E402

# BehaviorTree.CPP is vendored as source but linked as a prebuilt static
# library: nav2/ros2_shim/CMakeLists.txt imports
# `<BTCPP_ROOT>/build/libbehaviortree_cpp.a` (plus minitrace and tinyxml2 from
# the same build) rather than add_subdirectory-ing upstream, because upstream
# insists on ZeroMQ for its Groot2 interface. That archive used to be a
# prerequisite nobody wrote down -- a fresh clone configured fine on Windows
# only because the directory was already there from an earlier manual build,
# and on Linux it failed with "missing and no known rule to make it".
#
# The flags below are the ones that build is known good with, read back from
# its own CMakeCache.txt. They are the same on both platforms.
BTCPP_ROOT = (HERE.parent / "third_party" / "dl" / "btcpp_master"
              / "BehaviorTree.CPP-master")
BTCPP_BUILD = BTCPP_ROOT / "build"
BTCPP_ARCHIVES = {
    "libbehaviortree_cpp.a": BTCPP_BUILD / "libbehaviortree_cpp.a",
    "libminitrace.a": BTCPP_BUILD / "3rdparty" / "minitrace" / "libminitrace.a",
    "libtinyxml2.a": BTCPP_BUILD / "3rdparty" / "tinyxml2" / "libtinyxml2.a",
}
# BUILD_TESTING=OFF is not cosmetic: upstream does `include(CTest)`, so on
# Linux this comes out ON and tests/CMakeLists.txt then wants GTest, which
# the image does not have -- configure dies before it gets to the library.
BTCPP_FLAGS = ["-DBTCPP_SHARED_LIBS=OFF", "-DBTCPP_BUILD_TOOLS=OFF",
               "-DBTCPP_EXAMPLES=OFF", "-DBTCPP_GROOT_INTERFACE=OFF",
               "-DBTCPP_SQLITE_LOGGING=OFF", "-DBTCPP_ENABLE_ASAN=OFF",
               "-DBTCPP_ENABLE_TSAN=OFF", "-DBTCPP_ENABLE_UBSAN=OFF",
               "-DBUILD_TESTING=OFF"]


def ensure_btcpp(tools, env, build_type, jobs, rebuild=False) -> int:
    """Build the vendored BehaviorTree.CPP static library if it is missing."""
    missing = [name for name, path in BTCPP_ARCHIVES.items() if not path.is_file()]
    if not missing and not rebuild:
        print("[build] BehaviorTree.CPP already built (%s)" % BTCPP_BUILD, flush=True)
        return 0
    if not (BTCPP_ROOT / "CMakeLists.txt").is_file():
        print("[build] no BehaviorTree.CPP source at %s; the vendored tree is\n"
              "        needed before the shim can link (see nav2/README.md)"
              % BTCPP_ROOT, file=sys.stderr, flush=True)
        return 1
    if missing and BTCPP_BUILD.exists():
        print("[build] %s missing from an existing %s; reconfiguring it"
              % (", ".join(missing), BTCPP_BUILD), flush=True)
    configure = [str(tools.cmake), "-S", str(BTCPP_ROOT), "-B", str(BTCPP_BUILD),
                 "-G", "Ninja"]
    if tools.cxx is not None:
        configure.append("-DCMAKE_CXX_COMPILER="
                         + str(tools.cxx).replace("\\", "/"))
    configure += ["-DCMAKE_BUILD_TYPE=" + build_type] + BTCPP_FLAGS
    print(">>>", " ".join(configure), flush=True)
    rc = subprocess.run(configure, env=env).returncode
    if rc:
        print("[build] BehaviorTree.CPP configure failed", file=sys.stderr, flush=True)
        return rc
    build = [str(tools.cmake), "--build", str(BTCPP_BUILD), "--", "-j", str(jobs)]
    print(">>>", " ".join(build), flush=True)
    rc = subprocess.run(build, env=env).returncode
    if rc:
        print("[build] BehaviorTree.CPP build failed", file=sys.stderr, flush=True)
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("targets", nargs="*", help="cmake targets (default: everything)")
    ap.add_argument("--msys2", default=None,
                    help="MSYS2 root providing the toolchain; Windows only and "
                         "ignored elsewhere (default: %s or $CPU_MSYS2_ROOT)"
                         % default_msys2())
    ap.add_argument("--build-type", default="Release")
    ap.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    ap.add_argument("--no-configure", action="store_true", help="skip the cmake -S step")
    ap.add_argument("--no-btcpp", action="store_true",
                    help="do not build the vendored BehaviorTree.CPP first")
    ap.add_argument("--rebuild-btcpp", action="store_true",
                    help="rebuild BehaviorTree.CPP even if its archives are there")
    args = ap.parse_args()

    tools = find_tools(args.msys2)
    env = build_env(tools)

    print("[build] %s; %s, jobs %d" % (tools.label, pin(), args.jobs), flush=True)

    if not args.no_btcpp:
        rc = ensure_btcpp(tools, env, args.build_type, args.jobs, args.rebuild_btcpp)
        if rc:
            return rc

    if not args.no_configure:
        configure = [str(tools.cmake), "-S", str(HERE), "-B", str(BUILD_DIR),
                     "-G", "Ninja"]
        if tools.cxx is not None:
            # 路径里的反斜杠在 -D 里会被 CMake 当成转义，统一成正斜杠。
            configure.append("-DCMAKE_CXX_COMPILER="
                             + str(tools.cxx).replace("\\", "/"))
        configure.append("-DCMAKE_BUILD_TYPE=" + args.build_type)
        print(">>>", " ".join(configure), flush=True)
        rc = subprocess.run(configure, env=env).returncode
        if rc:
            print("[build] configure failed", file=sys.stderr, flush=True)
            return rc

    build = [str(tools.cmake), "--build", str(BUILD_DIR), "--",
             "-j", str(args.jobs)] + list(args.targets)
    print(">>>", " ".join(build), flush=True)
    return subprocess.run(build, env=env).returncode


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
