"""Talk to the ARM board over SSH: run commands, move files.

The board (a D-Robotics RDK X5 running Ubuntu 22.04 on aarch64) is reached
over the network once it has joined a Wi-Fi, and it has no key installed, so
the password is asked for the way `ssh` would ask for it - here through
paramiko instead of a terminal prompt.

Two things this script has to get right, because both bit us on the first
run:

* **The board prints UTF-8, the Windows console is GBK.** Printing the output
  as a `str` raises `UnicodeEncodeError` on the first Chinese character. The
  bytes are therefore written straight to `stdout.buffer`.
* **A background process dies with the session.** `nohup ... &` inside an
  `exec_command` is gone by the time the channel closes. The board's own
  `systemd-run --unit=...` is what keeps a daemon alive there.

Usage:
    python tools/ssh_board.py --run "uname -m"
    python tools/ssh_board.py --put local.tar.gz /tmp/local.tar.gz
    python tools/ssh_board.py --get /tmp/out.txt out.txt

The host and the credentials come from `--host` / `--user` / `--password`,
or from the environment (`BOARD_HOST`, `BOARD_USER`, `BOARD_PASSWORD`), so a
board on another address does not need a new script.
"""
from __future__ import annotations

import argparse
import os
import sys
import time

import paramiko

DEFAULT_HOST = "192.168.0.220"
DEFAULT_USER = "root"
DEFAULT_PASSWORD = "root"


class Board:
    """One SSH session to the board."""

    def __init__(self, host: str, user: str, password: str, timeout: float = 30.0) -> None:
        self.client = paramiko.SSHClient()
        # The board is on a lab network and has no key; the host key is
        # therefore accepted rather than pinned. A deployment that cares
        # should pass a known_hosts file instead.
        self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.client.connect(host, username=user, password=password,
                            timeout=timeout, banner_timeout=timeout,
                            auth_timeout=timeout, look_for_keys=False,
                            allow_agent=False)
        self.host = host

    def close(self) -> None:
        try:
            self.client.close()
        except Exception:
            pass

    def run(self, command: str, timeout: float = 120.0) -> tuple[int, str]:
        """Run one command, return (exit code, stdout and stderr)."""
        _stdin, stdout, stderr = self.client.exec_command(command, timeout=timeout)
        out = stdout.read().decode("utf-8", "replace")
        err = stderr.read().decode("utf-8", "replace")
        code = stdout.channel.recv_exit_status()
        return code, out + err

    def put(self, local: str, remote: str) -> None:
        """Upload a file. The remote directory has to exist."""
        sftp = self.client.open_sftp()
        try:
            sftp.put(local, remote)
        finally:
            sftp.close()

    def get(self, remote: str, local: str) -> None:
        sftp = self.client.open_sftp()
        try:
            sftp.get(remote, local)
        finally:
            sftp.close()


def _emit(text: str) -> None:
    """Write text as UTF-8 bytes, so a GBK console does not choke."""
    sys.stdout.buffer.write(text.encode("utf-8", "replace"))
    sys.stdout.buffer.write(b"\n")
    sys.stdout.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=os.environ.get("BOARD_HOST", DEFAULT_HOST),
                        help="board address (default %(default)s)")
    parser.add_argument("--user", default=os.environ.get("BOARD_USER", DEFAULT_USER))
    parser.add_argument("--password", default=os.environ.get("BOARD_PASSWORD",
                                                             DEFAULT_PASSWORD))
    parser.add_argument("--run", action="append", default=[], help="command (repeatable)")
    parser.add_argument("--put", nargs=2, metavar=("LOCAL", "REMOTE"), action="append",
                        default=[], help="upload a file (repeatable)")
    parser.add_argument("--get", nargs=2, metavar=("REMOTE", "LOCAL"), action="append",
                        default=[], help="download a file (repeatable)")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="seconds one command may take (default %(default)s)")
    args = parser.parse_args()

    board = Board(args.host, args.user, args.password)
    try:
        for local, remote in args.put:
            started = time.monotonic()
            board.put(local, remote)
            print("put %s -> %s (%.1f MB/s)" % (
                local, remote,
                os.path.getsize(local) / 1e6 / max(0.001, time.monotonic() - started)))
        for remote, local in args.get:
            board.get(remote, local)
            print("get %s -> %s" % (remote, local))
        for command in args.run:
            print("=== %s" % command)
            code, output = board.run(command, timeout=args.timeout)
            _emit(output.rstrip())
            print("[exit %d]" % code)
        return 0
    finally:
        board.close()


if __name__ == "__main__":
    sys.exit(main())