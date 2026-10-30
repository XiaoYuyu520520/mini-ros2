"""Negative control: disable the two `seek_to_latest()` calls, rebuild, run,
print the verdict, then put the lines back. Leaves the tree exactly as found."""
import io, os, subprocess, sys, time

ROOT = r"E:\DesktopData\app\code\CPU"
SERVICE = os.path.join(ROOT, r"nav2\ros2_shim\include\rclcpp\service.hpp")
CLIENT = os.path.join(ROOT, r"nav2\ros2_shim\include\rclcpp\client.hpp")
PYEXE = r"E:\venvs\py312\Scripts\python.exe"
BUILD = os.path.join(ROOT, r"nav2\ros2_shim\build.py")
EXE = os.path.join(ROOT, r"nav2\ros2_shim\build\test_channel_ring_lifecycle.exe")

PATCHES = [
    (SERVICE, "    request_channel_->seek_to_latest();\n",
     "    // NEGATIVE-CONTROL request_channel_->seek_to_latest();\n"),
    (CLIENT, "    response_channel_->seek_to_latest();\n",
     "    // NEGATIVE-CONTROL response_channel_->seek_to_latest();\n"),
]

originals = {}
for path, old, new in PATCHES:
    src = io.open(path, encoding="utf-8", newline="").read()
    assert "\r\n" not in src, path
    assert src.count(old) == 1, (path, src.count(old))
    originals[path] = src
    io.open(path, "w", encoding="utf-8", newline="\n").write(src.replace(old, new, 1))
print("[nc] both calls disabled")

try:
    rc = subprocess.run([PYEXE, "-X", "utf8", BUILD, "test_channel_ring_lifecycle"],
                        cwd=ROOT).returncode
    print("[nc] build rc", rc)
    if rc:
        raise SystemExit(2)

    env = dict(os.environ)
    env["PATH"] = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin" + os.pathsep + env["PATH"]
    env["NAV2_SHM_NAMESPACE"] = "negctl_ring"
    t = time.time()
    r = subprocess.run([EXE], env=env, capture_output=True, timeout=900)
    out = r.stdout.decode("utf-8", "replace")
    print("[nc] rc", r.returncode, "secs %.1f" % (time.time() - t))
    for line in out.splitlines():
        if "FAIL" in line or line.startswith("[test]") or "checks," in line:
            print("   ", line)
    io.open(os.path.join(ROOT, r".scratch\nc_ring_lifecycle.log"), "w",
            encoding="utf-8", newline="\n").write(out)
finally:
    for path, src in originals.items():
        io.open(path, "w", encoding="utf-8", newline="\n").write(src)
    print("[nc] restored")
    for path, old, new in PATCHES:
        src = io.open(path, encoding="utf-8", newline="").read()
        assert src.count(old) == 1 and src == originals[path], path
    print("[nc] both files byte-identical to before")