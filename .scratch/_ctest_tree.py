# Run ctest for one build tree, pinned to the six big cores.
import ctypes, os, subprocess, sys, time

CORES = [0, 1, 10, 11, 12, 13]
UCRT = r"D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin"
BUILD = sys.argv[1]

mask = 0
for cpu in CORES:
    mask |= 1 << cpu
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.GetCurrentProcess.restype = ctypes.c_void_p
k32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), mask)

argv = [os.path.join(UCRT, "ctest.exe"), "--output-on-failure"]
if len(sys.argv) > 2:
    argv += sys.argv[2:]
env = dict(os.environ)
env["PATH"] = UCRT + os.pathsep + env.get("PATH", "")
begin = time.monotonic()
done = subprocess.run(argv, cwd=BUILD, env=env, capture_output=True, text=True, errors="replace")
sys.stdout.write(done.stdout)
sys.stdout.write(done.stderr)
print("[ctest] rc=%d in %.1f s (mask 0x%X)" % (done.returncode, time.monotonic() - begin, mask))
