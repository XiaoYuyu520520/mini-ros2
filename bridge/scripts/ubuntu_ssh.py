#!/usr/bin/env python3
"""Run a shell command on the Ubuntu robot over SSH.

The bridge lives on Windows and the robot on Linux, so most maintenance steps
(restarting the simulator, reading its log, killing a stale process) are one
`ssh` call. Doing that from PowerShell is painful - the quoting of `$(...)`,
`>` and `|` gets rewritten before ssh ever sees it - so this helper keeps the
remote script byte exact: it is passed to `bash -s` on stdin.

Examples
--------
    python bridge/scripts/ubuntu_ssh.py "ps -ef | grep run_sim | grep -v grep"
    python bridge/scripts/ubuntu_ssh.py --file bridge/scripts/ubuntu_sim_ctl.sh -- status
    python bridge/scripts/ubuntu_ssh.py --timeout 30 "tail -n 20 ~/sim.log"

With `--file` the words after `--` are the script's own `$1`, `$2`, ...: they
ride along as `bash -s -- <args>`, so `ubuntu_sim_ctl.sh` sees the same
`start` / `stop` / `status` word a plain shell would give it. (Until this was
spelled out they were parsed and thrown away, which made every documented
`-- stop` and `-- status` call run the script with *no* verb - and
`ubuntu_sim_ctl.sh` defaults to `status`, so "stop" quietly did nothing.)
"""
from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
from pathlib import Path

DEFAULT_KEY = Path.home() / "Desktop" / "app" / "ssh_key" / "id_ed25519"
DEFAULT_TARGET = "ubuntu@192.168.1.113"
DEFAULT_WORKDIR = "~/code/CPU"


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", help="shell command to run on the robot")
    parser.add_argument("extra", nargs="*",
                        help="with --file: arguments handed to the script")
    parser.add_argument("--file", help="run this local script instead of `command`")
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--key", default=str(DEFAULT_KEY))
    parser.add_argument("--workdir", default=DEFAULT_WORKDIR,
                        help="cd here before running the script (default %(default)s)")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--quiet", action="store_true", help="do not echo the command")
    args = parser.parse_args()

    script_args: list[str] = []
    if args.file:
        script = Path(args.file).read_text(encoding="utf-8")
        # argparse hands the first word to `command` (the inline-command slot)
        # and the rest to `extra`; with --file every word is an argument.
        script_args = ([args.command] if args.command else []) + list(args.extra)
    elif args.command:
        if args.extra:
            parser.error("arguments after a command only make sense with --file")
        script = args.command
    else:
        parser.error("give a command or --file")

    payload = f"cd {args.workdir} 2>/dev/null; {script}\n"
    if not args.quiet:
        print(f"[ubuntu_ssh] {args.target}: {script.strip()}", file=sys.stderr, flush=True)
    remote = "bash -s"
    if script_args:
        remote += " -- " + " ".join(shlex.quote(a) for a in script_args)
    ssh = ["ssh", "-o", "StrictHostKeyChecking=no", "-o", "BatchMode=yes",
           "-i", args.key, args.target, remote]
    try:
        proc = subprocess.run(ssh, input=payload.encode("utf-8"), timeout=args.timeout)
    except subprocess.TimeoutExpired:
        print(f"[ubuntu_ssh] timed out after {args.timeout:g}s", file=sys.stderr)
        return 124
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())