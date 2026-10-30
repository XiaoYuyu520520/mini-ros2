#!/usr/bin/env python3
"""The "six big cores" rule, in one place.

This machine is an Intel Core Ultra 5 245K (Arrow Lake, no hyperthreading): 14
logical cores, 6 P-cores and 8 E-cores. Everything that compiles or runs tests
here has one standing requirement -- use the 6 big cores, never all 14 -- so the
detection and the pinning live in this module and the build scripts and
`tools/run_ctest.py` / `tools/ctest_stress.py` all call it.

`big_cores()` asks Windows for the topology (`GetLogicalProcessorInformationEx`,
`RelationProcessorCore`) and returns the members of the *smaller* efficiency
class, which is the six P-cores here: {0, 1, 10, 11, 12, 13}. The split is not
taken on faith from the MSDN rule of thumb; it was measured (400M dependent
volatile increments: 0.0811 s on one set, 0.0876 s on the other, the 8% that
5.2 GHz vs 4.6 GHz predicts) and it matches `EfficiencyClass` exactly.

`pin()` sets the affinity mask of the current process. Children inherit it, so
cmake / ninja / g++ / ctest and every test process they spawn stay on the big
cores.

Overrides::

    BIG6_CORES=0,1,10,11,12,13   override the detection
    BIG6_NO_PIN=1                detect, but do not pin
"""
from __future__ import annotations

import os
from pathlib import Path

DEFAULT_JOBS = 6


def big_cores() -> list[int]:
    """大核（P-core）的逻辑核编号，按从小到大。"""
    env = os.environ.get("BIG6_CORES")
    if env:
        cores = sorted(int(x) for x in env.replace(" ", "").split(",") if x)
        if cores:
            return cores
    if os.name != "nt":
        return list(range(min(DEFAULT_JOBS, os.cpu_count() or 1)))
    try:
        import ctypes
        import ctypes.wintypes as wt

        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        size = wt.DWORD(0)
        k32.GetLogicalProcessorInformationEx(0, None, ctypes.byref(size))
        if not size.value:
            raise OSError("no topology")
        buf = ctypes.create_string_buffer(size.value)
        if not k32.GetLogicalProcessorInformationEx(0, buf, ctypes.byref(size)):
            raise OSError("topology query failed")
        raw = buf.raw
        found: dict[int, int] = {}
        off = 0
        while off + 8 <= size.value:
            rel = int.from_bytes(raw[off:off + 4], "little")
            entry = int.from_bytes(raw[off + 4:off + 8], "little")
            if entry <= 0 or off + entry > size.value:
                break
            if rel == 0:  # RelationProcessorCore
                eff = raw[off + 9]
                # 条目内偏移 32 起是 GroupMask[]；只在 entry 范围内读，越界会把下一条目
                # 的 Relationship/Size 当成掩码，读出 36/37 这种不存在的 CPU 号。
                mask_off = off + 32
                while mask_off + 8 <= off + entry:
                    mask = int.from_bytes(raw[mask_off:mask_off + 8], "little")
                    while mask:
                        found[(mask & -mask).bit_length() - 1] = eff
                        mask &= mask - 1
                    mask_off += 8
            off += entry
        by_class: dict[int, list[int]] = {}
        for cpu, eff in found.items():
            by_class.setdefault(eff, []).append(cpu)
        # 规则是"取成员数较少的那一类"：6 个 P 核 vs 8 个 E 核。
        if len(by_class) > 1:
            return sorted(min(by_class.values(), key=len))
        if found:
            return sorted(found)[:DEFAULT_JOBS]
    except Exception:
        pass
    return list(range(min(DEFAULT_JOBS, os.cpu_count() or 1)))


def pin() -> str:
    """把当前进程钉到大核；子进程继承。返回一句日志文本。"""
    cores = big_cores()
    if os.name != "nt" or os.environ.get("BIG6_NO_PIN"):
        return "cores %s (not pinned)" % ",".join(map(str, cores))
    try:
        import ctypes

        mask = 0
        for cpu in cores:
            mask |= 1 << cpu
        # 必须声明 argtypes：不声明的话 ctypes 会把 HANDLE 当 32 位 int 传，
        # GetCurrentProcess 的伪句柄 (HANDLE)-1 就变成 0xFFFFFFFF，调用直接失败。
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        k32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        k32.SetProcessAffinityMask.restype = ctypes.c_int
        handle = ctypes.c_void_p(k32.GetCurrentProcess())
        if not k32.SetProcessAffinityMask(handle, mask):
            raise OSError(ctypes.get_last_error())
        return "cores %s (mask 0x%X, pinned)" % (",".join(map(str, cores)), mask)
    except Exception as exc:
        return "cores %s (NOT pinned: %s)" % (",".join(map(str, cores)), exc)


if __name__ == "__main__":
    print("big cores:", big_cores())
    print(pin())
