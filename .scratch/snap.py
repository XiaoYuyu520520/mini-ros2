import io, sys, time, os
p = r"bridge\windows\build\ros2_bridge_daemon.log"
def snap():
    try:
        s = io.open(p, encoding="utf-8", errors="replace").read().split("\n")
    except FileNotFoundError:
        return None
    idx = [i for i, l in enumerate(s) if l.startswith("[bridge] peer=")]
    if not idx:
        return None
    i = idx[-1]
    return s[i] + "\n" + "\n".join(l for l in s[i+1:i+8])
print(snap())
