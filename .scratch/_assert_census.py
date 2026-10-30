import io, os, re, json

SHIM = r"nav2\ros2_shim\CMakeLists.txt"
SLAM = r"nav2\slam_toolbox_port\CMakeLists.txt"

def read(p):
    return io.open(p, encoding="utf-8", errors="replace").read()

def exe_map(path, base):
    t = read(path)
    m = {}
    for mm in re.finditer(r"add_executable\(\s*([A-Za-z0-9_\-]+)\s+([^\n\)]+)\)", t):
        src = mm.group(2).strip().split()[0]
        m[mm.group(1)] = os.path.normpath(os.path.join(base, src))
    return m

def tests_of(path):
    t = read(path)
    out = []
    for mm in re.finditer(r"add_test\(\s*NAME\s+([A-Za-z0-9_\-]+)\s+COMMAND\s+([A-Za-z0-9_\-]+)", t):
        out.append((mm.group(1), mm.group(2)))
    return out

shim_base = os.path.dirname(SHIM)
slam_base = os.path.dirname(SLAM)
em = {}
em.update(exe_map(SHIM, shim_base))
em.update(exe_map(SLAM, slam_base))

# slam exes are also referenced in the shim CMakeLists without add_executable there
tests = tests_of(SHIM)

UP = re.compile(r"(?<![A-Za-z_>])(?:CHECK|EXPECT|ASSERT|REQUIRE)[A-Z_]*\(")
LO = re.compile(r"(?<![A-Za-z_>])(?:expect|check|verify|require_)[A-Za-z_]*\(")
DEF = re.compile(r"^\s*(?:void|bool|int|static\s+\w+)\s+(?:expect|check)\s*\(")

rows = []
miss = []
for name, cmd in tests:
    src = em.get(cmd)
    if not src or not os.path.exists(src):
        miss.append((name, cmd)); continue
    t = read(src)
    body = "\n".join(l for l in t.split("\n") if not DEF.match(l))
    up = len(UP.findall(t))
    lo = len(LO.findall(body))
    rows.append((name, os.path.relpath(src, "."), up, lo))

print("registered tests resolved:", len(rows), "unresolved:", miss)
tot_up = sum(r[2] for r in rows)
tot_lo = sum(r[3] for r in rows)
print("total UPPER:", tot_up, "total lower:", tot_lo, "sum:", tot_up + tot_lo)
zero = [r for r in rows if r[2] + r[3] == 0]
print("ZERO-assertion tests:", zero)
print()
w = max(len(r[0]) for r in rows)
for name, src, up, lo in sorted(rows, key=lambda r: (r[2] + r[3])):
    print("%-*s  %-44s upper=%-4d lower=%-4d tot=%d" % (w, name, src, up, lo, up + lo))
json.dump([{"test": r[0], "src": r[1], "upper": r[2], "lower": r[3]} for r in rows],
          io.open(r".scratch\_assert_census.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)