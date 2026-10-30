import io, os, re
d = r"gRPC\ros2_gRPC_sdk\tests"
for f in sorted(os.listdir(d)):
    p = os.path.join(d, f)
    t = io.open(p, encoding="utf-8", errors="replace").read()
    uses = {k: t.count(k) for k in ("ros2_shim", "shm_transport", "ShmChannel", "graph.hpp", "NAV2_SHM") if t.count(k)}
    inc = [m.group(1) for m in re.finditer(r'#include\s*[<"]([^">]+)[">]', t)
           if "ros2_shim" in m.group(1) or "shm" in m.group(1)]
    print("%-28s shim-usage=%s includes=%s" % (f, uses or "none", inc or "none"))
print()
print("--- bridge ctest dir coverage re-check ---")
for p in [r"nav2\ros2_shim\CMakeLists.txt", r"nav2\shm_transport\CMakeLists.txt",
          r"bridge\windows\CMakeLists.txt", r"gRPC\ros2_gRPC_sdk\CMakeLists.txt"]:
    t = io.open(p, encoding="utf-8", errors="replace").read()
    a = len(re.findall(r"add_test\s*\(", t))
    e = len(re.findall(r"ENVIRONMENT_MODIFICATION", t))
    loop = "PROPERTY TESTS)" in t
    print("%-42s add_test=%d  ENV_MOD=%d  dir-wide-loop=%s" % (p, a, e, loop))