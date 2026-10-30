# -*- coding: utf-8 -*-
"""Where do the graph reader's `ERROR_SHARING_VIOLATION`s come from?

`test_graph_rewrite` occasionally reports one read out of ~1500 as "not
openable", and now says why: every one of them is `ERROR_SHARING_VIOLATION`
(32) - never `ERROR_FILE_NOT_FOUND` (2), never an empty file, never a file
without `#end`. A sharing violation on an open that asks for maximum sharing
(`FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`) cannot come from the
reader's own sharing mode, and it cannot come from a rename window (that is
"file not found").

This probe is the writer-free half of the question, and takes the shim out of
it completely: a thread opens, reads and closes a file that nothing else
touches, as fast as it can, and counts how the opens come back.

    python .scratch/gr_selfrace.py [seconds] [threads] [writer]

Without `writer` there is no writer at all. With it, a second thread rewrites
the file the way `install_file` does - write `<path>.tmp`, then
`MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` - and the two halves are counted
apart, so the refusal rate can be read per open *and* per replace.
"""
from __future__ import annotations

import ctypes
import os
import sys
import tempfile
import threading
import time

GENERIC_READ = 0x80000000
FILE_SHARE_ALL = 0x00000001 | 0x00000002 | 0x00000004
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
CREATE_ALWAYS = 2
MOVEFILE_REPLACE_EXISTING = 0x1
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateFileW.restype = ctypes.c_void_p
kernel32.CreateFileW.argtypes = [
    ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
    ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
kernel32.ReadFile.argtypes = [
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
kernel32.WriteFile.argtypes = [
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
kernel32.MoveFileExW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32]
kernel32.ReplaceFileW.restype = ctypes.c_int
kernel32.ReplaceFileW.argtypes = [
    ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32,
    ctypes.c_void_p, ctypes.c_void_p]


def body(services: int) -> bytes:
    lines = ["#nav2shim-graph\t1"]
    for index in range(services):
        lines.append("service\t/rewrite_churn_%d\tstd_srvs/srv/Trigger\tlocal" % index)
    lines.append("#end")
    return ("\n".join(lines) + "\n").encode("utf-8")


def main() -> int:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    threads = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    with_writer = len(sys.argv) > 3 and sys.argv[3] == "writer"

    directory = os.path.join(tempfile.gettempdir(), "gr_selfrace")
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "probe.graph")
    temp = path + ".tmp"
    with open(path, "wb") as handle:
        handle.write(body(0))

    stop = threading.Event()
    lock = threading.Lock()
    opens = [0] * threads
    refused = [0] * threads
    other: dict[int, int] = {}
    renames = [0]

    def hammer(slot: int) -> None:
        buffer = ctypes.create_string_buffer(4096)
        read = ctypes.c_uint32(0)
        count = 0
        bad = 0
        while not stop.is_set():
            handle = kernel32.CreateFileW(
                path, GENERIC_READ, FILE_SHARE_ALL, None, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, None)
            count += 1
            if handle == INVALID_HANDLE_VALUE:
                code = ctypes.get_last_error()
                bad += 1
                with lock:
                    other[code] = other.get(code, 0) + 1
                continue
            while kernel32.ReadFile(handle, buffer, 4096, ctypes.byref(read), None) and read.value > 0:
                pass
            kernel32.CloseHandle(ctypes.c_void_p(handle))
        with lock:
            opens[slot] = count
            refused[slot] = bad

    # Exactly what `install_file` does, so the two halves of the shim's own
    # swap are counted apart: try `MoveFileEx` first (it either swaps the entry
    # or fails), fall back to `ReplaceFileW` (which holds the target open while
    # it works), then to a plain move when there was nothing to replace.
    def rewrite() -> None:
        moves = 0
        replaces = 0
        payload = body(1)
        while not stop.is_set():
            handle = kernel32.CreateFileW(
                temp, GENERIC_READ | 0x40000000, FILE_SHARE_ALL, None, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, None)
            if handle == INVALID_HANDLE_VALUE:
                with lock:
                    other[-2] = other.get(-2, 0) + 1
                continue
            written = ctypes.c_uint32(0)
            kernel32.WriteFile(handle, payload, len(payload), ctypes.byref(written), None)
            kernel32.CloseHandle(ctypes.c_void_p(handle))
            if kernel32.MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING):
                moves += 1
                continue
            if kernel32.ReplaceFileW(path, temp, None, 0x1, None, None):
                replaces += 1
                continue
            if kernel32.MoveFileExW(temp, path, 0):
                moves += 1
                continue
            with lock:
                other[-1] = other.get(-1, 0) + 1
        with lock:
            renames[0] = moves + replaces
            other[-3] = moves
            other[-4] = replaces

    workers = [threading.Thread(target=hammer, args=(i,)) for i in range(threads)]
    for worker in workers:
        worker.start()
    writer = None
    if with_writer:
        writer = threading.Thread(target=rewrite)
        writer.start()
    time.sleep(seconds)
    stop.set()
    for worker in workers:
        worker.join()
    if writer is not None:
        writer.join()

    total = sum(opens)
    print("file      : %s" % path)
    print("threads   : %d readers, writer=%s, %.0f s" % (threads, with_writer, seconds))
    print("opens     : %d" % total)
    print("refused   : %d (%.4f%%)" % (sum(refused), 100.0 * sum(refused) / max(total, 1)))
    print("swaps     : %d" % renames[0])
    if renames[0]:
        print("refused per 1000 replaces: %.1f" % (1000.0 * sum(refused) / renames[0]))
    names = {
        -1: "the swap failed outright",
        -2: "the temp file would not open",
        -3: "swaps that were a MoveFileEx",
        -4: "swaps that needed ReplaceFileW",
    }
    for code, count in sorted(other.items()):
        name = names.get(code, "CreateFile refused, code " + str(code))
        print("  %s: %d" % (name, count))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())