import io, json, sys, time
sys.path.insert(0, r"E:\DesktopData\app\code\CPU\bridge\scripts")
sys.stdout.reconfigure(encoding="utf-8")
import acceptance_check as ac

result = ac.Result("slam", "slam_toolbox maps a lap from the bridged /scan + /tf")
started = time.time()
try:
    ac.check_slam(result)
except Exception as exc:
    result.detail = "%s: %s" % (type(exc).__name__, exc)
result.seconds = time.time() - started
print("\n==== RESULT ====")
print("ok=%s  seconds=%.1f" % (result.ok, result.seconds))
print("detail: %s" % result.detail)
print("data: %s" % json.dumps(result.data, indent=2, ensure_ascii=False))
io.open(r"E:\DesktopData\app\code\CPU\.scratch\slam_check.json", "w", encoding="utf-8").write(
    json.dumps(result.as_dict(), indent=2, ensure_ascii=False))
sys.exit(0 if result.ok else 1)