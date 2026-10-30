import io, os, subprocess, time, json

ROOT = r"E:\DesktopData\app\code\CPU"
S = os.path.join(ROOT, ".scratch")
BATDIR = os.path.join(ROOT, "bridge", "windows")
OUT = os.path.join(S, "vo_run.txt")

def txt(p):
    return p.decode("gbk", "replace") if isinstance(p, bytes) else p

lines = []

def probe(title, args):
    p = subprocess.run("ros2.bat " + " ".join(args), shell=True, cwd=BATDIR,
                       capture_output=True, timeout=120)
    out = txt(p.stdout).rstrip()
    err = txt(p.stderr).rstrip()
    lines.append("$ ros2 " + " ".join(args))
    if out:
        lines.append(out)
    if err:
        lines.append("[stderr] " + err)
    lines.append("[exit %d]" % p.returncode)
    lines.append("")

server = subprocess.Popen([os.path.join(S, "vo_server.bat")], shell=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(3.0)
manager = subprocess.Popen([os.path.join(S, "vo_manager.bat")], shell=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(9.0)

probe("node list", ["node", "list"])
probe("topic list -t", ["topic", "list", "-t"])
probe("topic info vo_map", ["topic", "info", "vo_map"])
probe("service list", ["service", "list"])

sout = server.communicate(timeout=90)[0]
mout = manager.communicate(timeout=90)[0]

lines.append("===== vector_object_server.exe stdout =====")
lines.append(txt(sout).rstrip())
lines.append("")
lines.append("===== lifecycle_manager.exe stdout =====")
lines.append(txt(mout).rstrip())
lines.append("")

text = "\n".join(lines) + "\n"
io.open(OUT, "w", encoding="utf-8", newline="\n").write(text)
print(text[:6000])