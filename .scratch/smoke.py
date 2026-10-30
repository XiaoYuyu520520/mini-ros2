import subprocess, io, os, time, glob, re
LOG = r".scratch\stack_smoke.log"
with io.open(LOG, "w", encoding="utf-8", errors="replace") as fh:
    t0=time.time()
    p = subprocess.run([r"bridge\windows\nav2_stack.bat", "--duration", "75"],
                       cwd=r"E:\DesktopData\app\code\CPU", stdout=fh, stderr=subprocess.STDOUT,
                       shell=False, timeout=300)
    print("rc", p.returncode, "elapsed %.1f s" % (time.time()-t0))
out = io.open(LOG, encoding="utf-8", errors="replace").read()
print(out[-3000:])