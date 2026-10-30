import ctypes, os, tempfile

# A byte string like a Boost binary archive: arbitrary bytes, including 0x1A.
payload = bytes([0x00, 0x01, 0x02, 0x1A, 0x41, 0x42, 0x43, 0x0A, 0x44])
path = os.path.join(tempfile.gettempdir(), "ctz_probe.bin")
open(path, "wb").write(payload)

crt = ctypes.CDLL("ucrtbase.dll")
crt.fopen.restype = ctypes.c_void_p
crt.fopen.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
crt.fread.restype = ctypes.c_size_t
crt.fread.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_void_p]
crt.fclose.argtypes = [ctypes.c_void_p]

for mode in (b"r", b"rb"):
    fh = crt.fopen(path.encode(), mode)
    buf = ctypes.create_string_buffer(64)
    got = crt.fread(buf, 1, 64, fh)
    crt.fclose(fh)
    print("mode %-3s read %d of %d bytes: %s" % (mode.decode(), got, len(payload), buf.raw[:got].hex()))

print("payload                    :", payload.hex())
os.remove(path)