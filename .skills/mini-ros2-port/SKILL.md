---
name: mini-ros2-port
description: Use when working on this repo - a ROS2 + nav2 stack ported to Windows on shared memory instead of DDS. Covers the build entry points, the five test trees, the encoding rules every file has to follow, and the Windows pitfalls (timer granularity, single-writer channels, .bat vs .ps1) that silently break behaviour.
---

# mini-ros2：Windows 上的 ROS2 + nav2 移植

## 这是什么

把 ROS2 和 nav2 跑在 Windows 上，**不装 ROS2、不用 DDS**：话题走**共享内存**，
跨机走 **gRPC**。上游 nav2 源码**一行不改**地编译进来，缺的东西加在兼容层
（`nav2/ros2_shim/`）里。

配套的三个目录：

| 目录 | 角色 |
| --- | --- |
| `nav2/ros2_shim/` | 共享内存 rclcpp 兼容层 + 移植进来的 nav2（**主要的活在这里**） |
| `bridge/` | Windows 守护进程 + `ros2` 命令行 + Ubuntu 侧的仿真器 |
| `gRPC/ros2_gRPC_sdk/` | 跨机通信 SDK（proto + 客户端/服务端库） |

## 四条入口，四个入口点

**每个子目录都有自己的 `README.md`，先读那个。** 根 `README.md` 是总览，
`GETTING_STARTED.md` 是给新手的五分钟版。

```powershell
# Windows：构建
python nav2\ros2_shim\build.py            # 兼容层 + nav2（最慢，~15 分钟）
python bridge\scripts\build_bridge.py     # 桥 + ros2 命令行
python gRPC\ros2_gRPC_sdk\scripts\build.py

# 测试：五棵树，每棵一个入口
python tools\run_ctest.py shim      # nav2 兼容层（32 条）
python tools\run_ctest.py shm       # 共享内存传输（1 条）
python tools\run_ctest.py bridge    # 桥 + CLI（6 条）
python tools\run_ctest.py grpc      # 通信 SDK（4 条）
python tools\run_ctest.py sim       # 仿真器 pytest（71 条）

# 门禁（改完必跑）
python tools\check_docs.py --quiet
python tools\check_structure.py
python tools\find_scratch_refs.py --check
python nav2\patches\verify_patch.py
```

**只用 6 个大核**（`tools/_cores.py` 探测 P-core 并钉核）。内存紧张时加 `--jobs 1`。

## 铁律

1. **能不改上游 nav2 就不改。** 缺接口、缺类型、平台差异，加在
   `nav2/ros2_shim/include/ros2_shim/prelude.hpp`（每个 TU 被 `-include` 注入）
   或 `include/ros2_shim/` 里。
2. **必须改上游的**，写成 `nav2/patches/` 里的 patch，并让 `verify_patch.py` 能校验，
   否则下次换上游版本会悄悄丢掉。
3. **不要**用 `nav2_port_package(...)` 之外的方式排除上游文件；改用那个宏，
   不要把文件 stub 掉。
4. **改了上游就更新 patch**，别只改工作树。

## 编码：每个文件的规矩不一样，写错 Windows 上直接跑不起来

| 类型 | 编码 | 换行 | 为什么 |
| --- | --- | --- | --- |
| `.md` | **UTF-8 BOM** | LF | 没有 BOM 时 Windows 工具按 GBK 猜，整篇变乱码 |
| `.ps1` | **UTF-8 BOM** | CRLF | Windows PowerShell 5.1 没 BOM 就按 ANSI 读，中文全坏 |
| `.bat` | **cp936 (GBK)** | CRLF | cmd 按 OEM 代码页读 `.bat`，UTF-8 会打成「不是内部或外部命令」 |
| `.sh` / `.py` / `.cpp` | UTF-8（无 BOM） | LF | 常规 |
| `.gitattributes` | 强制 `* text=auto eol=lf`；`.bat` / `.ps1` 例外 | | |

**用 Python 改文件时最容易踩**：写进 C 字符串字面量的 `\n` 会被当成真换行
（如果混了行尾）。改完用 `python tools/check_docs.py` 和 `tools/fix_ps1_encoding.py` 验。

## Windows 上的四个坑（都会静默出错）

### 1. 定时器粒度 15.6 ms

`sleep_for(1ms)` 在 Windows 上实际睡 15.6 ms —— 1000 Hz 变 64 Hz。
凡是「速率不对」先怀疑这个：

```cpp
// 错
std::this_thread::sleep_for(std::chrono::milliseconds(1000 / rate));
// 对：按 deadline 推进，wait_until 睡得动的部分睡掉，最后 yield 转到点
rclcpp::shim::wait_until(next);
```

`nav2/ros2_shim/include/rclcpp/precise_wait.hpp` 是现成的。

### 2. 共享内存通道只有一个写者

一个通道只能有一个 writer。镜像（daemon 从远端灌进来）和上行（本地发布出去）
**不能是同一个话题**。`bridge_daemon.cpp` 里 uplink 的话题被显式排除出镜像集。

### 3. `.bat` 改不了 PowerShell 会话

`.bat` 跑在 cmd 子进程里，`set` 只影响子进程。PowerShell 里要**点源 `.ps1`**：

```powershell
. .\start\加载模拟ROS环境变量.ps1    # 点的 "." 不能省
ros2 topic list -t
```

### 4. 订阅要先于发布者存在

标准 ROS2 里订阅先建好、等发布者出现。`ros2 topic echo` **不能**只 `open` 通道
（没有发布者时返回 null 就退出），要 `create`（shim 的规则：谁先碰都行）。
类型也可能在发布者出现前是未知的，要惰性再查一次。

## 加一个跨机消息类型（6 步）

1. `nav2\ros2_shim\generate.ps1` 生成结构体（生成物在 `generated/`，不要手改）
2. `bridge/windows/src/type_registry.cpp` 加三处：编码分支、解码分支、表项
3. `python bridge\tests\generate_fixtures.py` 造基准字节（用真 ROS2 的 `rosbags`）
4. `bridge\windows\build\bridge_cdr_check.exe` 逐字节自检
5. `bridge/ubuntu/ros2_bridge_sim/messages.py` 让仿真真的发得出来
6. 跑 `tools\run_ctest.py bridge` 和仿真那棵 pytest

## 加一条 ctest

写进 `nav2/ros2_shim/tests/xxx.cpp`，在 `CMakeLists.txt` 里 `add_executable` +
`target_link_libraries(... ros2_shim)` + `add_test`。

- `NAV2_SHM_NAMESPACE` 会自动注入，**不用自己设**
- 要改断言时**先确认它会红**：改坏、看它红、再改回来
- `.probe.cpp` 是现场探针，**不注册**成 ctest

## 证据与文档

- `.scratch/` 是 `bridge/docs/` 的证据库。**被文档引用的路径必须留在原地**
  （`tools/find_scratch_refs.py --check` 会查）
- `attic/` 和 `_before/` **故意不入库**（归档区、生成的对比二进制），
  门禁只跳过这两个前缀的存在性检查
- 文档是**脚本产出的**，不是编辑器敲出来的

## 别踩的坑

- **别把构建产物提交进仓库**：`build/`、`dist/`、`.venv/`、`__pycache__` 都被 ignore
- **凭据永远不进仓库**：token / `*.pem` / `*.key` / `.env` 都被 ignore，
  提交前用 `git grep --cached -l 'github_pat_'` 扫一遍
- **别在 Windows 上改 Linux-only 的代码而不编**：`waitpid` 这类 POSIX 符号在
  Windows 分支里编不到，问题只在 CI 上暴露
