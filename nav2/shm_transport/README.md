# shm_transport · 共享内存通道

话题的实际载体：**单写者、多读者**的共享内存环形缓冲（每个读者有自己的游标，落后超过
`slot_count` 条就静默跳过并记进 `dropped_count()`），外加给 C 与 Python 调用方的薄封装。

算法只有一份（`src/shm_transport.cpp`），平台差异全部收在 `ShmChannel::Impl` 的成员与几个小函数里：
Windows 用命名文件映射 + 命名互斥体 + 手工事件，Linux 用 `shm_open` / `mmap` + 段内
`PTHREAD_PROCESS_SHARED|ROBUST` 互斥体 + 条件变量，段的生命周期靠段自身的 `flock` 共享锁。
两边的逐项对照，以及三个踩过的坑（`EOWNERDEAD` 必须当成「锁拿到了」、段的生命周期不能靠段内引用计数、
C ABI 加 `shm_channel_` 前缀），都在 `../README.md` 的「跨平台」一节。

## 布局

| 路径 | 作用 |
| --- | --- |
| `include/shm_transport/shm_channel.hpp` | C++ 接口：`create` / `open` / `publish` / `subscribe` / `seek_to_latest` / `seek_to_latched` / 计数 |
| `include/shm_transport/` 的 C ABI 头 | 给 Python 与 C 调用方（符号带 `shm_channel_` 前缀） |
| `src/shm_transport.cpp` | 唯一一份算法 + `Impl` 里的两套平台后端 |
| `python/shm_transport.py` | `ctypes` 封装，Python 侧互操作用例用它 |
| `tests/test_shm.cpp` | 唯一的 ctest：广播、跨进程、被强杀写者留下的段必须起一个空环、活写者的段不准被重置 |
| `build.py` | 构建入口，configure + build + ctest 一条龙；`--build-dir` 可以另开一棵 Debug 树 |

## 构建与测试

```bash
python nav2/shm_transport/build.py        # 配置 + 构建 + ctest（Windows / Linux 同一个入口）
python tools/run_ctest.py shm             # 只跑那 1 条
```

`ros2_shim` 与 `bridge\windows` 两棵树都把它作为子目录带进来，所以同一个用例会在三处出现 ——
各自带不同的 `NAV2_SHM_NAMESPACE`，不会互相抢通道。
