#!/usr/bin/env python3
"""Fetch the vendored build dependencies into nav2/third_party/dl/.

The shim needs two source trees that are *not* in version control:

  * `dl/btcpp_master/BehaviorTree.CPP-master` - the behaviour tree engine the
    nav2 port links against (nav2/ros2_shim/CMakeLists.txt points at it);
  * `dl/yamlcpp/yaml-cpp-0.8.0` - the YAML parser the parameter loader uses.

They are third-party sources, so they are fetched rather than committed - the
same rule the rest of `dl/` follows. This script is what a CI runner (or a
fresh clone) runs first.

It tries the GitHub tarball first and a set of mirrors after it, because a
runner on one side of the Great Firewall gets a 404 or a stall from GitHub
while a mirror answers immediately. Every mirror is tried in turn; the first
one that produces a usable tree wins.

    python nav2/third_party/dl/fetch_deps.py            # both
    python nav2/third_party/dl/fetch_deps.py btcpp yamlcpp
"""
from __future__ import annotations

import io
import os
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

DLDIR = Path(__file__).resolve().parent
TIMEOUT = 120

#: name -> (kind, upstream, destination relative to dl/)
DEPS = {
    "btcpp_master": (
        "github",
        "BehaviorTree/BehaviorTree.CPP",
        "btcpp_master/BehaviorTree.CPP-master",
    ),
    "yamlcpp": (
        "github",
        "jbeder/yaml-cpp",
        "yamlcpp/yaml-cpp-0.8.0",
    ),
}

#: The archive name to try, for each mirror flavour. `{paths}` expands to
#: `heads/<ref>` for a branch and `tags/<ref>` for a tag, which is the only
#: difference between the two.
MIRRORS = (
    "https://ghfast.top/https://github.com/{repo}/archive/{paths}.zip",
    "https://ghproxy.net/https://github.com/{repo}/archive/{paths}.zip",
    "https://hub.gitmirror.com/https://github.com/{repo}/archive/{paths}.zip",
    "https://github.com/{repo}/archive/{paths}.zip",
    "https://codeload.github.com/{repo}/zip/{paths}",
)

#: The upstream ref each dependency is taken from, and whether it is a branch
#: or a tag. `yaml-cpp-0.8.0` is the *directory* the vendored tree lives in;
#: the upstream tag is `0.8.0` (the `yaml-cpp-` prefix was the old naming
#: convention and stopped at 0.7). Asking for the wrong one is a 404 on
#: every mirror.
REFS = {
    "btcpp_master": ("branch", "master"),
    "yamlcpp": ("tag", "0.8.0"),
}


def _fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "mini-ros2-fetch"})
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
        return response.read()


def _extract(archive: bytes, destination: Path, want_prefix: str) -> None:
    """Unpack a zip/tarball, keeping only the top directory we want.

    GitHub names that directory after the repository and the ref, and the ref
    spelling differs between a branch (``repo-master``) and a tag
    (``repo-yaml-cpp-0.8.0``). So the prefix to keep is matched loosely: the
    directory whose name starts with the repository name.
    """
    destination.mkdir(parents=True, exist_ok=True)
    if archive[:2] == b"PK":
        with zipfile.ZipFile(io.BytesIO(archive)) as zf:
            roots = {name.split("/")[0] for name in zf.namelist() if "/" in name}
            root = _pick_root(roots, want_prefix)
            if root is None:
                raise RuntimeError("no directory named like %r in the archive" % want_prefix)
            for name in zf.namelist():
                parts = name.split("/")
                if len(parts) < 2 or parts[0] != root:
                    continue
                target = destination / "/".join(parts[1:])
                if name.endswith("/"):
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with zf.open(name) as src, open(target, "wb") as dst:
                    dst.write(src.read())
    else:
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:*") as tf:
            roots = {m.name.split("/")[0] for m in tf.getmembers() if "/" in m.name}
            root = _pick_root(roots, want_prefix)
            if root is None:
                raise RuntimeError("no directory named like %r in the archive" % want_prefix)
            for member in tf.getmembers():
                parts = member.name.split("/")
                if len(parts) < 2 or parts[0] != root:
                    continue
                target = destination / "/".join(parts[1:])
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                extracted = tf.extractfile(member)
                if extracted is None:
                    continue
                with extracted as src, open(target, "wb") as dst:
                    dst.write(src.read())


def _pick_root(roots: set, want_prefix: str) -> str | None:
    """The archive's top directory, matched by prefix."""
    for candidate in sorted(roots):
        if candidate.lower().startswith(want_prefix.lower()):
            return candidate
    return None


def fetch(name: str) -> bool:
    kind, repo, relative = DEPS[name]
    ref_kind, ref = REFS[name]
    destination = DLDIR / relative
    if (destination / "CMakeLists.txt").is_file():
        print("[fetch] %-12s already present" % name)
        return True

    paths = ("heads/" if ref_kind == "branch" else "tags/") + ref
    # GitHub names the archive's top directory "<repo>-<ref>", so the repo's
    # own name is the prefix to match against.
    want_prefix = repo.split("/")[-1]
    for template in MIRRORS:
        url = template.format(repo=repo, paths=paths)
        print("[fetch] %-12s trying %s" % (name, url.split("/")[2]))
        try:
            archive = _fetch(url)
        except Exception as exc:  # noqa: BLE001 - any failure means "try the next"
            print("[fetch] %-12s failed: %s" % (name, exc))
            continue
        try:
            _extract(archive, destination, want_prefix)
        except Exception as exc:  # noqa: BLE001
            print("[fetch] %-12s could not unpack: %s" % (name, exc))
            continue
        if (destination / "CMakeLists.txt").is_file():
            print("[fetch] %-12s OK -> %s" % (name, destination))
            return True
        print("[fetch] %-12s archive had no CMakeLists.txt" % name)
    print("[fetch] %-12s FAILED - no mirror answered" % name, file=sys.stderr)
    return False


def main(argv: list[str]) -> int:
    wanted = argv[1:] or list(DEPS)
    unknown = [name for name in wanted if name not in DEPS]
    if unknown:
        print("unknown dependency: %s (known: %s)" % (", ".join(unknown), ", ".join(DEPS)),
              file=sys.stderr)
        return 2
    failures = [name for name in wanted if not fetch(name)]
    if failures:
        print("[fetch] still missing: %s" % ", ".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
