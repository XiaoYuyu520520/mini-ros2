import ctypes, os, sys, threading, time
sys.path.insert(0, r"E:\DesktopData\app\code\CPU\nav2\shm_transport\python")
os.environ.pop("SHM_TRANSPORT_LIB", None)
import shm_transport as st

lib, platform = st.path_parts()
print("loaded", lib._name, platform)
w = st.ShmChannel.create("py_ctypes_check", 64, 4096)
print("create ok", w.published_count)
assert w.publish(b"hello shm")
r = st.ShmChannel.open("py_ctypes_check")
got = r.subscribe(capacity=64, timeout_ms=1000)
print("subscribe ->", got, "published", r.published_count, "read", r.read_count)
assert got == b"hello shm", got
w.close()
r.close()
print("python ctypes ABI ok")