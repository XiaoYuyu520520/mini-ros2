import io, os, re

def read(p):
    return io.open(p, encoding="utf-8", errors="replace").read()

UP = re.compile(r"(?<![A-Za-z_>])(?:CHECK|EXPECT|ASSERT|REQUIRE)[A-Z_]*\(")
LO = re.compile(r"(?<![A-Za-z_>])(?:expect|check|verify|require_|failures)[A-Za-z_]*\(")

files = [
    r"bridge\windows\tests\cdr_check.cpp",
    r"bridge\windows\tests\service_roundtrip.cpp",
    r"bridge\windows\tests\action_roundtrip.cpp",
    r"nav2\shm_transport\tests\test_shm.cpp",
]
tot = 0
for p in files:
    if not os.path.exists(p):
        print("MISSING", p); continue
    t = read(p)
    up = len(UP.findall(t)); lo = len(LO.findall(t))
    tot += up + lo
    print("%-52s upper=%-4d lower=%-4d tot=%d  bytes=%d" % (p, up, lo, up + lo, len(t)))
print("extra total:", tot)

grpc = r"gRPC\ros2_gRPC_sdk\tests"
g = 0
for f in sorted(os.listdir(grpc)):
    t = read(os.path.join(grpc, f))
    c = len(re.findall(r"(?<![A-Za-z_])check\(", t))
    g += c
print("grpc lower-case check() call sites:", g)

# python side
pyt = r"bridge\ubuntu\ros2_bridge_sim\tests"
n = 0
for f in sorted(os.listdir(pyt)):
    if not f.endswith(".py"):
        continue
    t = read(os.path.join(pyt, f))
    k = len(re.findall(r"\n\s*def test_", t))
    n += k
    print("  %-34s test funcs %d" % (f, k))
print("python test funcs:", n)