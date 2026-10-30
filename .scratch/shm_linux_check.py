#!/usr/bin/env python3
"""Build and run the nav2/shm_transport tests on the Ubuntu box, from Windows.

The POSIX backend of the shared-memory transport (nav2/README.md, "跨平台") is
built and tested on the robot, not here, so this is the one command that
reproduces the Linux half of the port:

    python .scratch/shm_linux_check.py              # push, configure, build, run
    python .scratch/shm_linux_check.py --runs 5     # and run it that many times
    python .scratch/shm_linux_check.py --push-only  # only copy the sources

It tars `nav2/shm_transport` over ssh (build directories excluded), configures
with Ninja, builds on 6 cores, and runs `./test_shm` with the same
`NAV2_SHM_NAMESPACE` the ctest entry injects. It finishes by listing the
leftover `/dev/shm/nav2shm_*` segments: the count has to be 0, and the
reference-counted `shm_unlink` in `ShmChannel::Impl::close` is what makes it so.
"""
from __future__ import annotations

import argparse
import io
import os
import pathlib
import subprocess
import tarfile

REPO_ROOT = pathlib.Path(r"E:\DesktopData\app\code\CPU")
KEY = os.path.expanduser(r"~/Desktop/app/ssh_key/id_ed25519")
TARGET = "ubuntu@192.168.1.113"
REMOTE_ROOT = "~/code/CPU"


def ssh(command: str, stdin: bytes | None = None, timeout: float = 600.0) -> int:
    argv = ["ssh", "-o", "StrictHostKeyChecking=no", "-o", "BatchMode=yes",
            "-i", KEY, TARGET, command]
    done = subprocess.run(argv, input=stdin or b"", timeout=timeout)
    return done.returncode


def push() -> int:
    base = REPO_ROOT / "nav2" / "shm_transport"
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for path in sorted(base.rglob("*")):
            rel = path.relative_to(base)
            if "build" in rel.parts or "__pycache__" in rel.parts:
                continue
            tar.add(str(path), arcname=str(pathlib.PurePosixPath("shm_transport").joinpath(*rel.parts)))
    data = buf.getvalue()
    remote = ("mkdir -p %s/nav2 && rm -rf %s/nav2/shm_transport && "
              "tar -xzf - -C %s/nav2 && echo pushed %d bytes" % (REMOTE_ROOT, REMOTE_ROOT, REMOTE_ROOT, len(data)))
    print("[push] %d bytes of tar" % len(data), flush=True)
    return ssh(remote, stdin=data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runs", type=int, default=1, help="how many times to run test_shm")
    parser.add_argument("--push-only", action="store_true")
    parser.add_argument("--jobs", type=int, default=6, help="build parallelism (big cores only)")
    args = parser.parse_args()

    code = push()
    if code != 0 or args.push_only:
        return code

    script = """
set -e
cd {root}/nav2/shm_transport
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /tmp/shm_cmake.log 2>&1 || {{ tail -20 /tmp/shm_cmake.log; exit 1; }}
cmake --build build -j{jobs} 2>&1 | tail -6
for i in $(seq 1 {runs}); do
  if NAV2_SHM_NAMESPACE=ctest_shm_transport ./build/test_shm > /tmp/shm_run_$i.log 2>&1; then
    echo "run $i: pass"
  else
    echo "run $i: FAIL"; tail -20 /tmp/shm_run_$i.log; exit 1
  fi
done
tail -n 12 /tmp/shm_run_1.log
echo "--- leftover /dev/shm segments (want 0):"
ls /dev/shm | grep -c nav2shm || true
ctest --test-dir build --output-on-failure 2>&1 | tail -3
""".format(root=REMOTE_ROOT, jobs=args.jobs, runs=args.runs)
    return ssh(script)


if __name__ == "__main__":
    raise SystemExit(main())