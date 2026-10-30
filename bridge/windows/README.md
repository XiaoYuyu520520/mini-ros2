# bridge\windows · 桥的两平台源码 + Windows 入口

**目录名容易误会**：这里放的是桥上**两边都编**的 C++ 源码 —— `src/`、`include/`、`tests/`
在 Linux 上也用这同一份 `CMakeLists.txt` 构建（`tools/check_cross_platform.py` 每轮都会验）。
真正 Windows-only 的只有那 12 个 `*.bat` / `*.ps1` 入口，以及把 MSYS2 的 DLL 收到 exe 旁边的
`cmake/bundle_runtime.cmake`；`../linux/` 里是与之对应的四个 `.sh`。

## 布局

| 路径 | 作用 |
| --- | --- |
| `src/bridge_daemon.cpp` | 守护进程：LAN 快速扫描、持久 gRPC 连接、双向镜像、图维护 |
| `src/type_registry.cpp` | CDR ↔ 共享内存转码 + 跨机类型表 |
| `src/service_registry.cpp`、`src/action_registry.cpp` | 跨机 service / action 的转发与编解码 |
| `include/ros2_bridge/` | 上面这些的头：`shm_topic.hpp`（通道命名与读写）、`cdr_archive.hpp`、`transcode.hpp`、`yaml_assign.hpp` … |
| `tools/ros2_cli.cpp` | `ros2` 命令本体：topic / service / action / param / node / interface |
| `tests/` | 5 条 ctest 的源码：`bridge_cdr_check`、`service_roundtrip`、`action_roundtrip`、`cli_values`、`shm_roundtrip` |
| `cmake/bundle_runtime.cmake` | 把 MSYS2 的 gRPC / protobuf / abseil DLL 复制到 build/ 旁边，好让 exe 在没有 MSYS2 的机器上也能跑 |
| `*.bat` / `*.ps1` | 12 个入口，逐个的作用见 `../README.md` 的「Windows 侧入口一览」；其中 `nav2_stack.bat` / `slam_demo.bat` / `amcl_demo.bat` / `nav2_safety.bat` / `nav2_docking_route.bat` 各有跨平台孪生 `../scripts/run_nav2_stack.py` / `run_slam_demo.py` / `run_amcl_demo.py` / `run_nav2_safety.py` / `run_nav2_docking_route.py`（Linux 也跑它们），两份是同一件事，改一个要改另一个 |

## 构建与测试

```bat
python bridge\scripts\build_bridge.py --test   :: 配置 + 构建 + ctest（6 条，约 4 s）
python tools\run_ctest.py bridge               :: 只跑 ctest
```

**重编前先停守护进程**：`bridge\windows\ros2_bridge.bat stop`。不停的话 exe 被占用，ninja 会报
`subcommand failed`，看着像编译错误。
