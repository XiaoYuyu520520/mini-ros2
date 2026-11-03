"""Talk to the ARM board over its serial console.

The board is reachable over the UART (COM3, 115200 8N1) before it has any
network: the console is the only way in while it boots, and it is also the
way to tell it which Wi-Fi to join.

What this script does:

* opens the port and waits for the login prompt,
* logs in (the board asks for a password; `root` on this image),
* runs commands and prints their output.

Two details that are not obvious:

* **The board echoes what it receives**, so every line of output appears
  twice: once as the echo of the command, once as the result. The reader
  therefore looks for a sentinel the command prints and takes what comes
  after it.
* **A command that blocks makes the session unusable.** `nmcli device wifi
  rescan` is one example: it does not return to the prompt within the
  timeout, and everything sent after it is swallowed. Give such a command its
  own invocation, or run it in the background on the board.

Usage:
    python tools/serial_shell.py --port COM3 --command "uname -a"
    python tools/serial_shell.py --port COM3 --command "ls /" --timeout 120

The port, the user and the password are options; they are also read from the
environment (`BOARD_PORT`, `BOARD_USER`, `BOARD_PASSWORD`).
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import time

import serial

DEFAULT_PORT = "COM3"
DEFAULT_BAUD = 115200
DEFAULT_USER = "root"
DEFAULT_PASSWORD = "root"

#: Printed after every command; the output is what follows it.
SENTINEL = "__MINI_ROS2_DONE__"


def strip_ansi(text: str) -> str:
    """Drop the escape sequences the console emits."""
    return re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", text)


class SerialShell:
    """A login session on the board's console."""

    def __init__(self, port: str, baud: int, user: str, password: str,
                 timeout: float = 60.0) -> None:
        self.serial = serial.Serial(port, baudrate=baud, timeout=0.2)
        self.user = user
        self.password = password
        self.timeout = timeout
        self.buffer = ""

    def close(self) -> None:
        try:
            self.serial.close()
        except Exception:
            pass

    def read_some(self) -> None:
        waiting = self.serial.in_waiting
        if waiting:
            self.buffer += self.serial.read(waiting).decode("utf-8", "replace")

    def wait_for(self, pattern: str, timeout: float | None = None) -> bool:
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        compiled = re.compile(pattern)
        while time.monotonic() < deadline:
            if compiled.search(strip_ansi(self.buffer)):
                return True
            self.read_some()
            time.sleep(0.05)
        return False

    def send_line(self, text: str) -> None:
        self.serial.write((text + "\n").encode("utf-8"))
        self.serial.flush()

    def drain(self, seconds: float) -> str:
        """Collect whatever arrives for a while and return it."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.read_some()
            time.sleep(0.05)
        out = strip_ansi(self.buffer)
        self.buffer = ""
        return out

    def login(self) -> bool:
        """Get to a shell prompt, logging in if the board asks."""
        self.drain(2.0)
        prompt = r"login:|~ #|\$ $|# "
        if not self.wait_for(prompt, timeout=15.0):
            # A board that boots into a console without a login prompt still
            # needs one newline to print its first prompt.
            self.send_line("")
            if not self.wait_for(prompt, timeout=15.0):
                return False
        if not self.wait_for(r"[Pp]assword:", timeout=1.0):
            # Already at a shell: sending the user name would be run as a
            # command, and the board answers "root: command not found".
            return True
        self.send_line(self.user)
        if not self.wait_for(r"[Pp]assword:", timeout=15.0):
            return True
        self.send_line(self.password)
        return self.wait_for(r"~ #|\$ $|# ", timeout=20.0)

    def run(self, command: str) -> str:
        """Run one command and return its output."""
        self.send_line("%s; echo %s:$?" % (command, SENTINEL))
        self.wait_for(re.escape(SENTINEL) + r":", timeout=self.timeout)
        time.sleep(0.3)
        self.read_some()
        text = strip_ansi(self.buffer)
        self.buffer = ""
        return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("BOARD_PORT", DEFAULT_PORT))
    parser.add_argument("--baud", type=int, default=int(os.environ.get("BOARD_BAUD", DEFAULT_BAUD)))
    parser.add_argument("--user", default=os.environ.get("BOARD_USER", DEFAULT_USER))
    parser.add_argument("--password", default=os.environ.get("BOARD_PASSWORD",
                                                             DEFAULT_PASSWORD))
    parser.add_argument("--command", action="append", default=[],
                        help="command to run (repeatable)")
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="seconds one command may take (default %(default)s)")
    parser.add_argument("--no-login", action="store_true",
                        help="the session is already logged in")
    args = parser.parse_args()

    shell = SerialShell(args.port, args.baud, args.user, args.password, args.timeout)
    try:
        if not args.no_login and not shell.login():
            print("could not log in on %s" % args.port, file=sys.stderr)
            print(strip_ansi(shell.buffer)[-800:], file=sys.stderr)
            return 2
        if not args.command:
            print(shell.drain(1.0))
            return 0
        for command in args.command:
            print("=== %s" % command)
            print(shell.run(command))
        return 0
    finally:
        shell.close()


if __name__ == "__main__":
    sys.exit(main())