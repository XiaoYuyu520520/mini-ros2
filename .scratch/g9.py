import io
L = io.open(r"nav2\nav2_extracted\navigation2-1.5.1\nav2_graceful_controller\src\graceful_controller.cpp", encoding="utf-8", errors="replace").read().splitlines()
for i in range(28, 95):
    print("%4d| %s" % (i + 1, L[i]))
print("...")
for i in range(630, min(645, len(L))):
    print("%4d| %s" % (i + 1, L[i]))