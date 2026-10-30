"""Read the Windows bridge environment the way ``ros2.ps1`` does.

A `.bat` cannot set variables in the caller, so a Python process that wants
the bridge's values (the shared-memory geometry, `ROS2_BRIDGE_BUILD`, ...)
cannot just call it: the values would die with the `cmd.exe` child. The
trick `Import-Ros2Env` uses is to run the bat and then ask that same child
to print its environment, and copy the interesting names back.

The `cmd /c "call ... && set"` form is avoided on purpose: `cmd` re-quotes a
command line that contains quotes, and the escapes it produces break `call`.
A one-line wrapper `.bat` written next to the process sidesteps the quoting
entirely.

`bridge_env.bat` stays the single source of truth - nothing here repeats a
value, so the two cannot drift.
"""
from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BRIDGE_ENV = REPO_ROOT / "bridge" / "windows" / "bridge_env.bat"

#: Only these are taken back. Importing the whole environment would drag
#: cmd's private variables (PROMPT, ComSpec, ...) into the process for no
#: reason.
_PREFIXES = ("ROS2_", "NAV2_")


def read_bridge_env() -> dict[str, str]:
    """``{NAME: value}`` for every ROS2_/NAV2_ variable ``bridge_env.bat`` sets."""
    if not BRIDGE_ENV.is_file():
        return {}

    # A wrapper batch file: no quoting to get wrong, and `set` runs in the
    # same cmd as the `call` so it sees the variables the call produced.
    wrapper = Path(tempfile.gettempdir()) / "_bridge_env_probe.bat"
    wrapper.write_text(
        "@echo off\r\n"
        'call "%s" >nul\r\n'
        "set\r\n" % BRIDGE_ENV,
        encoding="ascii",
    )
    try:
        out = subprocess.run(
            [os.environ.get("SystemRoot", r"C:\Windows") + r"\System32\cmd.exe",
             "/c", str(wrapper)],
            capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return {}
    finally:
        try:
            wrapper.unlink()
        except OSError:
            pass

    env: dict[str, str] = {}
    for line in out.stdout.splitlines():
        name, sep, value = line.partition("=")
        if sep and name.startswith(_PREFIXES):
            env[name] = value
    return env


def bridge_env() -> dict[str, str]:
    """The current process environment with ``bridge_env.bat``'s values applied."""
    merged = dict(os.environ)
    merged.update(read_bridge_env())
    return merged