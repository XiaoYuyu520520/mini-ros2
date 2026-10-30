import os, re, sys, io
rx = re.compile(sys.argv[1])
for target in sys.argv[2:]:
    files = []
    if os.path.isdir(target):
        for dp, _, fs in os.walk(target):
            for f in fs:
                files.append(os.path.join(dp, f))
    else:
        files = [target]
    for p in files:
        try:
            txt = io.open(p, encoding="utf-8", errors="replace").read()
        except Exception:
            continue
        for i, line in enumerate(txt.splitlines(), 1):
            if rx.search(line):
                print("%s:%d: %s" % (p.replace("\\", "/"), i, line.rstrip()))
