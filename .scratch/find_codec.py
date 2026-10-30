import io, os, re
base = r"E:\DesktopData\app\code\CPU\bridge"
for root, dirs, files in os.walk(base):
    dirs[:] = [d for d in dirs if d not in ("build", "CMakeFiles")]
    for f in files:
        p = os.path.join(root, f)
        try:
            txt = io.open(p, encoding="utf-8", errors="replace").read()
        except Exception:
            continue
        if "supported_services" in txt or "find_service_codec" in txt:
            print(p.replace(r"E:\DesktopData\app\code\CPU"+"\\", ""), os.path.getsize(p))