---
layout: default
title: mini-ros2
---

# mini-ros2 · 在 Windows 上跑 ROS2 与 nav2

> **在 Windows 上像在 Linux 一样用 ROS2 和 nav2 —— 不用装 ROS2，不用 DDS。**
> Linux 上的机器人（或仿真小车）通过局域网 gRPC 把话题送到 Windows，
> Windows 上的 nav2 建图、规划，再把 `/cmd_vel` 送回去，小车动起来。

**[English](#english) · [中文](#中文)**

| | |
| --- | --- |
| 仓库 | [github.com/XiaoYuyu520520/mini-ros2](https://github.com/XiaoYuyu520520/mini-ros2) |
| 最新发布 | [Releases](https://github.com/XiaoYuyu520520/mini-ros2/releases) |
| 许可证 | MIT |
| 主语言 | C++17（兼容层 / 桥 / SDK）、Python（仿真 / 中继 / 脚本） |
| 支持平台 | Windows 10/11 x86_64（主体）、Ubuntu 24.04 x86_64 |

---

## 它能干什么

| 能力 | 状态 |
| --- | --- |
| Windows 上用 `ros2 topic list` / `echo` / `pub` / `service call` / `action send_goal` / `param` | ✅ 不用装 ROS2 |
| Windows 上跑 **nav2 全套**（规划、控制、行为树、AMCL、SLAM、平滑、航点、安全链） | ✅ 上游 nav2 1.5.1 源码原地编译 |
| Windows ↔ Linux 之间**双向**传话题 / 服务 / 动作 | ✅ 局域网 gRPC |
| Ubuntu 上「真 ROS2」和「仿真」两个图**合并成一个**给 Windows | ✅ `start/connector.sh` |
| 同一话题两边都发布时**自动分开**（`/scan` 与 `/real/scan`） | ✅ 不丢数据 |
| 运行期调参（参数服务 + `ros2 param`） | ✅ 对着正在跑的节点读写 |
| 一键打包发布（Windows / Linux 两个包） | ✅ CI 自动出 |

## 它**不是**什么

- **不是官方 ROS2**。接口兼容（`rclcpp` 头文件、消息类型、`ros2` 命令行），底层是
  **共享内存 + gRPC**，不是 DDS。所以不能和真正的 ROS2 节点在**同一台机器上**直接互相发现
  （需要走本项目的中继）；没有完整的 QoS、DDS 发现、多播；`launch` 文件语法只有一小部分。
- **不是零拷贝**。一次发布有 4 次内存拷贝。准确说法是**零系统调用（zero-syscall）**。
  实测 `/scan`（5.8 KB）延迟 ~8.8 µs、吞吐 ~88k msg/s，nav2 的 20–50 Hz 完全够用，
  但大点云/图像不划算。
- **不保证和上游 nav2 行为 100% 一致**。缺失的功能逐条记在 [限制与缺口](limitations.html)。

---

## 快速开始

### 用发布包（推荐新手）

去 [Releases](https://github.com/XiaoYuyu520520/mini-ros2/releases) 下载
`mini-ros2-windows-x86_64-<版本>.zip`，解压后**保持路径原样**：

```powershell
# Windows / PowerShell，点源一次
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

```bash
# Linux
source start/加载模拟ROS环境变量.sh
ros2 topic list -t
```

### 从源码编译

```powershell
python nav2/third_party/dl/fetch_deps.py        # 先拉两个第三方源码树
python nav2/ros2_shim/build.py                  # nav2 + 兼容层
python bridge/scripts/build_bridge.py --test    # 桥 + ros2 命令行
```

编完跑五棵测试树（每一步都应该是绿的）：

```bash
python tools/run_ctest.py shim     # nav2 兼容层
python tools/run_ctest.py shm      # 共享内存传输
python tools/run_ctest.py bridge   # 桥
python tools/run_ctest.py grpc     # 通信 SDK
python tools/run_ctest.py sim      # 仿真器（pytest）
```

### 连上机器人

```bash
# Ubuntu：起仿真 + agent，再起中继
bash bridge/scripts/ubuntu_sim_ctl.sh start
bash start/connector.sh up -d
```

```powershell
# Windows：连过去
$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'   # 换成你的 Ubuntu 地址
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

---

## 文档导航

| 页面 | 内容 |
| --- | --- |
| [快速开始](getting-started.html) | 三分钟跑起来，能干什么 / 不能干什么 |
| [架构](architecture.html) | 三个子项目怎么拼起来，数据怎么流 |
| [构建与运行](build-and-run.html) | 逐项目编译 / 启动 / 停止速查（Windows 与 Linux 对照） |
| [ros2 命令行](cli.html) | `topic` / `service` / `action` / `param` / `node` 全部用法 |
| [能力清单](capabilities.html) | 建图 / 定位 / 规划 / 控制 / 行为树 / 安全链，逐条实测数据 |
| [限制与缺口](limitations.html) | 还差什么、代价多少、按什么优先级补 |
| [验收与证据](validation.html) | 验收标准、实测数字、怎么复现 |
| [开发指南](development.html) | 目录结构、改上游的规矩、加消息类型 / 加测试的步骤 |
| [排错](troubleshooting.html) | 常见故障与定位手段 |

---

# English

> **Run ROS2 and nav2 on Windows as if you were on Linux — no ROS2 install, no DDS.**
> A robot (or a simulated car) on Linux ships its topics over LAN gRPC to Windows;
> nav2 on Windows maps, plans, and sends `/cmd_vel` back so the car moves.

| | |
| --- | --- |
| Repository | [github.com/XiaoYuyu520520/mini-ros2](https://github.com/XiaoYuyu520520/mini-ros2) |
| Releases | [github.com/XiaoYuyu520520/mini-ros2/releases](https://github.com/XiaoYuyu520520/mini-ros2/releases) |
| License | MIT |
| Language | C++17 (shim / bridge / SDK), Python (simulator / relay / scripts) |
| Platforms | Windows 10/11 x86_64 (primary), Ubuntu 24.04 x86_64 |

## What it does

| Capability | Status |
| --- | --- |
| `ros2 topic list` / `echo` / `pub` / `service call` / `action send_goal` / `param` on Windows | ✅ no ROS2 install |
| The **full nav2 stack** on Windows (planning, control, behavior trees, AMCL, SLAM, smoothing, waypoints, safety) | ✅ upstream nav2 1.5.1 compiled in place |
| **Bidirectional** topics / services / actions between Windows and Linux | ✅ LAN gRPC |
| Merging the **real ROS2 graph** and the **simulator graph** on Ubuntu into one graph for Windows | ✅ `start/connector.sh` |
| Auto-separating same-named topics when both sides publish (`/scan` vs `/real/scan`) | ✅ no data loss |
| Runtime parameter tuning (parameter services + `ros2 param`) | ✅ reads/writes a *running* node |
| One-command release packaging (Windows + Linux) | ✅ CI produces both |

## What it is **not**

- **Not official ROS2.** The interfaces are compatible (`rclcpp` headers, message types, `ros2` CLI),
  but the substrate is **shared memory + gRPC**, not DDS. Consequence: it cannot discover real ROS2
  nodes **on the same machine** without this project's relay; no full QoS, no DDS discovery, no multicast;
  only a small subset of `launch` syntax.
- **Not zero-copy.** A publish performs 4 memory copies. More precisely it is **zero-syscall**.
  Measured: `/scan` (5.8 KB) latency ~8.8 µs, throughput ~88k msg/s — plenty for nav2's 20–50 Hz,
  but not for large point clouds or images.
- **Not guaranteed 100% identical to upstream nav2.** Every gap is listed page by page in
  [Limitations](limitations.html).

## Quick start

### Use a release package (recommended for newcomers)

Download `mini-ros2-windows-x86_64-<version>.zip` from
[Releases](https://github.com/XiaoYuyu520520/mini-ros2/releases) and keep the extracted layout as-is:

```powershell
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

```bash
source start/加载模拟ROS环境变量.sh
ros2 topic list -t
```

### Build from source

```powershell
python nav2/third_party/dl/fetch_deps.py        # fetch two third-party source trees
python nav2/ros2_shim/build.py                  # nav2 + compatibility layer
python bridge/scripts/build_bridge.py --test    # bridge + ros2 CLI
```

Then run the five test trees (each should be green):

```bash
python tools/run_ctest.py shim     # nav2 compatibility layer
python tools/run_ctest.py shm      # shared-memory transport
python tools/run_ctest.py bridge   # bridge
python tools/run_ctest.py grpc     # communication SDK
python tools/run_ctest.py sim      # simulator (pytest)
```

### Connect to a robot

```bash
# Ubuntu: start the simulator + agent, then the relay
bash bridge/scripts/ubuntu_sim_ctl.sh start
bash start/connector.sh up -d
```

```powershell
# Windows: point at it
$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

## Documentation

| Page | Contents |
| --- | --- |
| [Getting Started](getting-started.html) | Three minutes to a running system; what it can and cannot do |
| [Architecture](architecture.html) | How the three sub-projects fit together and how data flows |
| [Build & Run](build-and-run.html) | Per-project build / launch / stop cheatsheet (Windows ↔ Linux) |
| [ros2 CLI](cli.html) | Every `topic` / `service` / `action` / `param` / `node` command |
| [Capabilities](capabilities.html) | Mapping, localization, planning, control, BT, safety — with measured numbers |
| [Limitations](limitations.html) | What is still missing, what it costs, and the priority order |
| [Validation](validation.html) | Acceptance criteria, measured evidence, how to reproduce |
| [Development](development.html) | Layout, rules for touching upstream, adding a message type / a test |
| [Troubleshooting](troubleshooting.html) | Common failures and how to diagnose them |
