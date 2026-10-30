import os, subprocess, io

ROOT = r"E:\DesktopData\app\code\CPU"
BATDIR = os.path.join(ROOT, "bridge", "windows")
OUT = os.path.join(ROOT, ".scratch", "param_e2e.txt")

CMDS = [
    ["node", "list"],
    ["param", "list", "/bt_navigator"],
    ["param", "list", "/bt_navigator", "-t"],
    ["param", "get", "/bt_navigator", "use_sim_time"],
    ["param", "describe", "/bt_navigator", "use_sim_time"],
    ["param", "get", "/controller_server", "controller_frequency"],
    ["param", "set", "/controller_server", "controller_frequency", "15.5"],
    ["param", "get", "/controller_server", "controller_frequency"],
    ["param", "get", "/controller_server", "min_x_velocity_threshold"],
    ["param", "set", "/controller_server", "controller_frequency", "not_a_number"],
    ["param", "get", "/controller_server", "controller_frequency"],
    ["param", "set", "/controller_server", "no_such_param", "1.0"],
    ["param", "get", "/controller_server", "no_such_param"],
    ["param", "describe", "/controller_server", "controller_frequency"],
    ["param", "describe", "/controller_server", "no_such_param"],
    ["param", "dump", "/bt_navigator"],
]

def run(args, timeout=180):
    p = subprocess.run("ros2.bat " + " ".join(args), shell=True, cwd=BATDIR,
                       capture_output=True, timeout=timeout)
    return p.returncode, p.stdout.decode("gbk", "replace"), p.stderr.decode("gbk", "replace")

lines = []
for args in CMDS:
    rc, out, err = run(args)
    lines.append("$ ros2 " + " ".join(args))
    body = out.rstrip("\r\n")
    if body:
        lines.append(body)
    if err.strip():
        lines.append("[stderr] " + err.rstrip("\r\n"))
    lines.append("[exit %d]" % rc)
    lines.append("")

text = "\n".join(lines) + "\n"
io.open(OUT, "w", encoding="utf-8", newline="\n").write(text)
print(text)