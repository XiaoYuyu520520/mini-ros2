#!/usr/bin/env python3
"""Where the build tools are on this machine.

Every entry point in this repo used to answer that question itself, and every
one of them answered it with the same two hardcoded assumptions: that the
compiler is MSYS2's UCRT64 `bin` directory, and that every tool ends in `.exe`.
Both are true on this desk and false everywhere else, which is how a repo whose
C++ is written for two platforms ended up with build scripts that could only
ever run on one.

This module is the single answer. On Windows it resolves MSYS2 UCRT64 (the
prefix that also carries the gRPC/protobuf the bridge links against); anywhere
else it resolves the system compiler, CMake and Ninja from `PATH`. Callers get
absolute paths plus the `bin` directory to prepend to `PATH`, and never build a
`".exe"` string themselves.

    from _toolchain import find_tools, build_env
    tools = find_tools()                    # or find_tools(msys2_root=...)
    print(tools.label, tools.cmake, tools.ctest)
    subprocess.run([tools.cmake, "-S", src, "-B", build], env=build_env(tools))

Overrides:::

    <VAR>_CMAKE / <VAR>_CXX / <VAR>_NINJA  ... no: use the plain names below.
    CMAKE=/path/to/cmake    CXX=/usr/bin/clang++    NINJA=/usr/bin/ninja
    CPU_MSYS2_ROOT=D:/MSYS2  the MSYS2 root on Windows (default below)

`cxx` is None when the platform default is good enough, which is the normal
case off Windows: CMake picking `cc`/`c++` is one fewer absolute path to keep
working, and `CXX` still overrides it.
"""
from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import NamedTuple

IS_WINDOWS = os.name == "nt"

# The MSYS2 the Windows builds have always used: it provides the compiler *and*
# gRPC/protobuf/abseil, which is why the bridge can link at all here.
DEFAULT_MSYS2 = Path("D:/WindowsServer2025_1/software/MSYS2")

EXE = ".exe" if IS_WINDOWS else ""


class Toolchain(NamedTuple):
    label: str
    bindir: Path | None      # prepended to PATH; None for the native toolchain
    cmake: Path
    cxx: Path | None         # None: let CMake choose (POSIX default)
    ninja: Path | None
    ctest: Path | None

    def path(self) -> str:
        return str(self.cxx) if self.cxx else "default compiler"


def default_msys2() -> Path:
    configured = os.environ.get("CPU_MSYS2_ROOT")
    return Path(configured) if configured else DEFAULT_MSYS2


def _which(*names: str) -> Path | None:
    for name in names:
        found = shutil.which(name)
        if found:
            return Path(found)
    return None


def _require(name: str, found: Path | None, hint: str) -> Path:
    if found is None:
        raise SystemExit(
            "missing %s -- %s\n"
            "Install it, or point CMAKE / CXX / NINJA at an existing one." % (name, hint))
    return found


def find_tools(msys2_root: Path | str | None = None, need_ninja: bool = True) -> Toolchain:
    """Resolve cmake/ninja/ctest for this platform.

    Windows: `<msys2_root>/ucrt64/bin`, all four tools, and a `bindir` the
    caller must prepend to `PATH` (`build_env` does it) so that the GNU runtime
    DLLs sit next to whatever runs.

    POSIX: `cmake`, `ninja`, `ctest` and (unless `CXX` says otherwise) the
    default compiler, straight from `PATH`.
    """
    if IS_WINDOWS:
        root = Path(msys2_root) if msys2_root else default_msys2()
        ucrt = root / "ucrt64" / "bin"
        names = ("cmake.exe", "ninja.exe", "ctest.exe", "g++.exe")
        missing = [str(ucrt / n) for n in names if not (ucrt / n).is_file()]
        if missing:
            raise SystemExit(
                "missing MSYS2 UCRT64 tools:\n  " + "\n  ".join(missing) +
                "\nPass --msys2 <root>, or set CPU_MSYS2_ROOT.")
        cmake = ucrt / "cmake.exe"
        ninja = ucrt / "ninja.exe"
        ctest = ucrt / "ctest.exe"
        cxx = Path(os.environ["CXX"]) if os.environ.get("CXX") else ucrt / "g++.exe"
        if os.environ.get("CMAKE"):
            cmake = Path(os.environ["CMAKE"])
        if os.environ.get("NINJA"):
            ninja = Path(os.environ["NINJA"])
        return Toolchain("MSYS2 UCRT64 at %s" % root, ucrt, cmake, cxx, ninja, ctest)

    cmake = Path(os.environ["CMAKE"]) if os.environ.get("CMAKE") else _require(
        "cmake", _which("cmake"), "Debian/Ubuntu: apt install cmake")
    ctest = _which("ctest")
    if ctest is None:
        ctest = cmake.parent / "ctest"
        ctest = ctest if ctest.exists() else None
    ninja = None
    if need_ninja:
        ninja = Path(os.environ["NINJA"]) if os.environ.get("NINJA") else _require(
            "ninja", _which("ninja"), "Debian/Ubuntu: apt install ninja-build")
    cxx = Path(os.environ["CXX"]) if os.environ.get("CXX") else None
    if cxx is None:
        compiler = _which("c++", "g++", "clang++")
        if compiler is None:
            raise SystemExit("no C++ compiler on PATH (apt install g++), or set CXX")
        cxx = None  # the system default is the one we just found; do not pin it
    label = "system toolchain (%s)" % (_which("c++", "g++", "clang++") or "c++")
    return Toolchain(label, None, cmake, cxx, ninja, ctest)


def build_env(tools: Toolchain, base: dict | None = None) -> dict:
    """`os.environ` with the toolchain `bin` first on PATH (Windows)."""
    env = dict(base or os.environ)
    if tools.bindir is not None:
        env["PATH"] = str(tools.bindir) + os.pathsep + env.get("PATH", "")
    return env