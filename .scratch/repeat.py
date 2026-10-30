import subprocess, os, time, re
root = os.getcwd()
res = []
for i in range(5):
    p = subprocess.run(["cmd", "/c",
        r'call bridge\windows\bridge_env.bat >nul 2>&1 && '
        r'nav2\ros2_shim\build\nav2_waypoints.exe --goal auto --count 3 --timeout 45 '
        r'--report .scratch\wp_rep%d.json' % i],
        cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)
    out = p.stdout
    m = re.search(r"  result     : (\S+)", out)
    t = re.search(r"travelled  : ([^\n]*)", out)
    c = re.search(r"pipeline   : ([^\n]*)", out)
    print(i, "rc", p.returncode, m.group(1) if m else "?", "|", t.group(1) if t else "", flush=True)
    print("   ", c.group(1) if c else "", flush=True)
    res.append(m.group(1) if m else "?")
print("results:", res)
