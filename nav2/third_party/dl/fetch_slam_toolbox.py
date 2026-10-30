#!/usr/bin/env python3
"""Fetch a GitHub repo tarball through mirrors, with speed probing."""
import os, sys, time, tarfile, urllib.request

REPO = "SteveMacenski/slam_toolbox"
BRANCH = sys.argv[1] if len(sys.argv) > 1 else "jazzy"
DLDIR = r"E:\DesktopData\app\code\CPU\nav2\third_party\dl"
DEST = r"E:\DesktopData\app\code\CPU\nav2\third_party\slam_toolbox"
OUT = os.path.join(DLDIR, "slam_toolbox-%s.tar.gz" % BRANCH)

GH = "https://github.com/%s/archive/refs/heads/%s.tar.gz" % (REPO, BRANCH)
URLS = [
    "https://ghfast.top/" + GH,
    "https://ghproxy.net/" + GH,
    "https://gh-proxy.com/" + GH,
    "https://hub.gitmirror.com/" + GH,
    "https://codeload.github.com/%s/tar.gz/refs/heads/%s" % (REPO, BRANCH),
    GH,
]

PROBE_SECONDS = 8
MIN_RATE = 80 * 1024


def probe(url):
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
    t0 = time.time()
    try:
        resp = urllib.request.urlopen(req, timeout=15)
    except Exception as exc:
        return None, "connect failed: %s" % exc
    total = 0
    chunks = []
    while time.time() - t0 < PROBE_SECONDS:
        try:
            buf = resp.read(65536)
        except Exception as exc:
            return None, "read failed: %s" % exc
        if not buf:
            break
        chunks.append(buf)
        total += len(buf)
    rate = total / max(time.time() - t0, 1e-6)
    return (resp, chunks, total, rate), None


def main():
    os.makedirs(DLDIR, exist_ok=True)
    best = None
    for url in URLS:
        print("[probe] %s" % url, flush=True)
        got, err = probe(url)
        if err:
            print("        -> %s" % err, flush=True)
            continue
        resp, chunks, total, rate = got
        print("        -> %.1f KB/s, %d bytes" % (rate / 1024, total), flush=True)
        if best is None or rate > best[3]:
            best = (url, resp, chunks, rate)
        if rate >= MIN_RATE:
            break
    if best is None:
        print("ALL MIRRORS FAILED")
        return 2
    url, resp, chunks, rate = best
    print("[chosen] %s @ %.1f KB/s" % (url, rate / 1024), flush=True)
    total = 0
    with open(OUT, "wb") as fh:
        for c in chunks:
            fh.write(c)
            total += len(c)
        while True:
            buf = resp.read(262144)
            if not buf:
                break
            fh.write(buf)
            total += len(buf)
    print("[saved] %s (%d bytes)" % (OUT, total), flush=True)

    if os.path.isdir(DEST):
        import shutil
        shutil.rmtree(DEST)
    os.makedirs(DEST, exist_ok=True)
    with tarfile.open(OUT, "r:gz") as tf:
        members = tf.getmembers()
        roots = sorted({m.name.split("/")[0] for m in members})
        print("[tar] roots=%s members=%d" % (roots, len(members)), flush=True)
        for m in members:
            parts = m.name.split("/", 1)
            if len(parts) == 1:
                continue
            m.name = parts[1]
            if m.name.startswith("/") or ".." in m.name.split("/"):
                raise RuntimeError("unsafe member %s" % m.name)
            tf.extract(m, DEST)
    print("[extracted] %s" % DEST, flush=True)
    print("[done]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
