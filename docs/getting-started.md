---
layout: default
title: 快速开始 Getting Started
---

# 快速开始 Getting Started

> **[中文](#中文) · [English](#english)**

---

# 中文

五分钟看懂：**这个项目能干什么、在什么机器上能跑、怎么跑起来。**

## 1. 它能干什么

一句话：**把 ROS2 和 nav2 搬到 Windows 上跑，让 Linux 上的机器人或仿真通过局域网和它通信。**

| 能力 | 状态 |
| --- | --- |
| 在 Windows 上用 `ros2 topic list` / `echo` / `pub` / `service call` / `action send_goal` / `param` | ✅ 不用装 ROS2 |
| 在 Windows 上跑 **nav2 全套**（规划、控制、行为树、AMCL、SLAM、平滑、航点、安全链） | ✅ 上游源码编译，不改逻辑 |
| Windows 和 Linux 之间**双向**传话题 / 服务 / 动作 | ✅ 局域网 gRPC |
| Ubuntu 上「真 ROS2」和「仿真」两个图**合并成一个**给 Windows | ✅ `start/connector.sh` |
| 同一话题两边都发布时**自动分开**（`/scan` 和 `/real/scan`） | ✅ 不丢数据 |
| 运行期调参（参数服务 + `ros2 param`） | ✅ 对着正在跑的节点读写 |
| Windows 编译 + 打包成一键运行的文件 | ✅ CI 自动出 Windows / Linux 两个包 |

### 它**不是**什么

- **不是官方 ROS2**。接口兼容（`rclcpp` 的头文件、消息类型、`ros2` 命令行），但底层是
  **共享内存 + gRPC**，不是 DDS。所以：
  - 不能和真正的 ROS2 节点在**同一台机器上**直接互相发现（需要走本项目的中继）；
  - 没有完整的 QoS、DDS 发现、多播；
  - `launch` 文件语法只有一小部分。
- **不是零拷贝**。一次发布有 4 次内存拷贝（结构体→字节→共享内存→字节→结构体）。
  准确说是**零系统调用**。实测 `/scan`（5.8 KB）延迟 ~8.8 µs、吞吐 ~88k msg/s，
  nav2 的 20–50 Hz 完全够用，但大点云/图像不划算。
- **不保证和上游 nav2 行为 100% 一致**。缺失的功能逐条记在 [限制与缺口](limitations.html)。

## 2. 能跑在哪

| 平台 | 说明 |
| --- | --- |
| **Windows 10/11 x86_64** | 主体。编译好之后**不需要 MSYS2、不需要 ROS2**，DLL 都在包里 |
| **Ubuntu 24.04 x86_64** | 兼容层和桥都能编能跑；CI 也会出 Linux 包 |
| 其它 Linux | 没测过。需要 g++ 13+、CMake 3.20+ |

跑之前需要的东西：

- **Windows 包**：什么都不用装，解压就能用（里面带 88 个 DLL）。
- **从源码编译**：Python 3.12、CMake、Ninja、一个 C++20 编译器。
  Windows 上推荐 MSYS2 UCRT64；Linux 上 `apt install` 见 [构建与运行](build-and-run.html)。
- **和机器人通信**：两台机器在同一个局域网，Ubuntu 侧能 SSH。

## 3. 最快的跑法

### 3.1 直接用发布包（推荐新手）

去 [Releases](https://github.com/XiaoYuyu520520/mini-ros2/releases) 下载：

- `mini-ros2-windows-x86_64-<版本>.zip`
- `mini-ros2-linux-x86_64-<版本>.tar.gz`

解压后（路径**保持原样**，别把 exe 挪走 —— 脚本按相对路径找文件）：

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

> **为什么必须点源（`.` 前缀）**：`.bat` 跑在 cmd.exe 子进程里，`set` 只影响那个子进程，
> 子进程一退变量跟着走。点源在**当前会话**里执行，变量和函数才会留下来。
> 「执行」不是「点源」。

### 3.2 从源码编译

```powershell
# Windows
python nav2/third_party/dl/fetch_deps.py        # 先拉两个第三方源码树
python nav2/ros2_shim/build.py                  # nav2 + 兼容层
python bridge/scripts/build_bridge.py --test    # 桥 + ros2 命令行
```

```bash
# Linux（先装依赖，见构建与运行）
python3 nav2/third_party/dl/fetch_deps.py
python3 nav2/ros2_shim/build.py
python3 bridge/scripts/build_bridge.py --test
```

编完跑测试（每一步都应该是绿的）：

```bash
python tools/run_ctest.py shim     # nav2 兼容层
python tools/run_ctest.py shm      # 共享内存传输
python tools/run_ctest.py bridge   # 桥
python tools/run_ctest.py grpc     # 通信 SDK
python tools/run_ctest.py sim      # 仿真器（pytest）
```

### 3.3 和机器人 / 仿真连起来

两个终端，**先 Linux 后 Windows**：

```bash
# Ubuntu：起仿真 + agent，再起中继
bash bridge/scripts/ubuntu_sim_ctl.sh start
bash start/connector.sh up -d
```

```powershell
# Windows：连过去
$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'   # 换成你的 Ubuntu 地址
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t                                 # 应该能看到机器人上的话题
```

> **顺序很重要**：Windows 侧扫不到对端时，最常见的原因是 Ubuntu 侧还没起来。
> 另外「发现」默认**不看本机**，所以守护进程和 agent 在同一台机器上时必须给地址。

### 3.4 跑一个完整任务

```powershell
# 复位小车（起栈前必须做，见下）
ros2 service call /sim/reset std_srvs/srv/Trigger

# 起整条 nav2 栈并开到目标点
python bridge/scripts/run_nav2_stack.py --goal auto --report goal.json

# 停栈
python bridge/scripts/run_nav2_stack.py stop
```

> **为什么每次起栈前都要 `/sim/reset`**：AMCL 拿 `/odom` 当初始位姿，车停在别处会让
> 每一次规划都报 `Start Coordinates ... was outside bounds`，不重启栈不会自愈。

## 4. 目录里都是什么

```
start/                 一键入口：加载环境、起连接器、起 Windows 服务
bridge/                Windows 守护进程 + ros2 命令行 + Ubuntu 仿真 + 文档
gRPC/ros2_gRPC_sdk/    跨机通信 SDK（proto + 客户端/服务端库）
nav2/ros2_shim/        共享内存 ROS2 兼容层 + 移植的 nav2
nav2/shm_transport/    共享内存环形缓冲（两平台一套算法）
nav2/slam_toolbox_port/  slam_toolbox 的 Windows 移植
code/Desktop/racecar/  小车的建图 + 导航工作区（参数、launch、航点脚本、地图）
tools/                 仓库工具：跑测试、文档体检、结构检查
package.py             打发布包
```

每个子目录都有自己的 `README.md`，写的是「这一层为什么长这样」。

## 5. 接下来读哪一页

| 你想做的事 | 去哪一页 |
| --- | --- |
| 搞懂三个子项目怎么拼 | [架构](architecture.html) |
| 编译 / 启动 / 停止 | [构建与运行](build-and-run.html) |
| 会用 `ros2` 命令 | [ros2 命令行](cli.html) |
| 看 nav2 到底能跑到什么程度 | [能力清单](capabilities.html) |
| 知道它**不**支持什么 | [限制与缺口](limitations.html) |
| 想复现验收 | [验收与证据](validation.html) |
| 要改代码 | [开发指南](development.html) |
| 跑不起来 | [排错](troubleshooting.html) |

---

# English

Five minutes to understand: **what this project does, where it runs, how to run it.**

## 1. What it does

**Bring ROS2 and nav2 onto Windows, so a robot or simulator on Linux can talk to it over the LAN.**

| Capability | Status |
| --- | --- |
| `ros2 topic list` / `echo` / `pub` / `service call` / `action send_goal` / `param` on Windows | ✅ no ROS2 install |
| The **full nav2 stack** on Windows (planning, control, behavior trees, AMCL, SLAM, smoothing, waypoints, safety) | ✅ upstream source compiled, logic untouched |
| **Bidirectional** topics / services / actions between Windows and Linux | ✅ LAN gRPC |
| Merging the **real ROS2 graph** and the **simulator graph** on Ubuntu into one for Windows | ✅ `start/connector.sh` |
| Auto-separating same-named topics when both sides publish (`/scan` vs `/real/scan`) | ✅ no data loss |
| Runtime parameter tuning (parameter services + `ros2 param`) | ✅ reads/writes a *running* node |
| Windows build + one-command packaging | ✅ CI emits Windows and Linux packages |

### What it is **not**

- **Not official ROS2.** The interfaces are compatible (`rclcpp` headers, message types, `ros2` CLI),
  but the substrate is **shared memory + gRPC**, not DDS. Consequently:
  - it cannot discover real ROS2 nodes **on the same machine** without this project's relay;
  - no full QoS, no DDS discovery, no multicast;
  - only a small subset of `launch` syntax.
- **Not zero-copy.** A publish performs 4 memory copies (struct→bytes→shared memory→bytes→struct).
  More precisely it is **zero-syscall**. Measured: `/scan` (5.8 KB) latency ~8.8 µs, throughput
  ~88k msg/s — plenty for nav2's 20–50 Hz, but not worth it for large point clouds or images.
- **Not guaranteed 100% identical to upstream nav2.** Every gap is listed in
  [Limitations](limitations.html).

## 2. Where it runs

| Platform | Notes |
| --- | --- |
| **Windows 10/11 x86_64** | Primary. After building, **no MSYS2 and no ROS2 needed** — the DLLs ship in the package |
| **Ubuntu 24.04 x86_64** | The shim and the bridge both configure, build and test there; CI also emits a Linux package |
| Other Linux | Untested. Needs g++ 13+, CMake 3.20+ |

Before you start:

- **Windows package**: install nothing. Unzip and run (88 DLLs included).
- **Building from source**: Python 3.12, CMake, Ninja, a C++20 compiler.
  MSYS2 UCRT64 is recommended on Windows; the Linux `apt install` line is in
  [Build & Run](build-and-run.html).
- **Talking to a robot**: two machines on the same LAN, SSH access to the Ubuntu side.

## 3. Fastest path

### 3.1 Use a release package (recommended for newcomers)

Download from [Releases](https://github.com/XiaoYuyu520520/mini-ros2/releases):

- `mini-ros2-windows-x86_64-<version>.zip`
- `mini-ros2-linux-x86_64-<version>.tar.gz`

Extract, and **keep the layout as-is** — do not move the exes, the scripts find them by relative path:

```powershell
# Windows / PowerShell, dot-source once
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

```bash
# Linux
source start/加载模拟ROS环境变量.sh
ros2 topic list -t
```

> **Why dot-sourcing (the `.` prefix) is mandatory**: a `.bat` runs in a cmd.exe child process;
> `set` only affects that child, and the variables die with it. Dot-sourcing executes in the
> **current session**, so the variables and functions stay. "Execute" is not "dot-source".

### 3.2 Build from source

```powershell
# Windows
python nav2/third_party/dl/fetch_deps.py        # fetch two third-party source trees
python nav2/ros2_shim/build.py                  # nav2 + compatibility layer
python bridge/scripts/build_bridge.py --test    # bridge + ros2 CLI
```

```bash
# Linux (install the deps first — see Build & Run)
python3 nav2/third_party/dl/fetch_deps.py
python3 nav2/ros2_shim/build.py
python3 bridge/scripts/build_bridge.py --test
```

Then run the tests (every step should be green):

```bash
python tools/run_ctest.py shim     # nav2 compatibility layer
python tools/run_ctest.py shm      # shared-memory transport
python tools/run_ctest.py bridge   # bridge
python tools/run_ctest.py grpc     # communication SDK
python tools/run_ctest.py sim      # simulator (pytest)
```

### 3.3 Connect to a robot / the simulator

Two terminals, **Linux first**:

```bash
# Ubuntu: simulator + agent, then the relay
bash bridge/scripts/ubuntu_sim_ctl.sh start
bash start/connector.sh up -d
```

```powershell
# Windows: point at it
$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'   # your Ubuntu address
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

> **Order matters**: the most common reason Windows cannot find the peer is that the Ubuntu side
> is not up yet. Also, discovery deliberately **does not look at this host**, so when the daemon and
> the agent share a machine you must pass the address explicitly.

### 3.4 Run a full mission

```powershell
# Reset the car (mandatory before starting the stack — see below)
ros2 service call /sim/reset std_srvs/srv/Trigger

# Bring up the whole nav2 stack and drive to a goal
python bridge/scripts/run_nav2_stack.py --goal auto --report goal.json

# Stop it
python bridge/scripts/run_nav2_stack.py stop
```

> **Why `/sim/reset` before every stack start**: AMCL takes `/odom` as its initial pose. If the car
> is parked elsewhere, every planning attempt reports
> `Start Coordinates ... was outside bounds`, and restarting the stack does not heal it.

## 4. What is in the directory

```
start/                 one-shot entry points: load env, start relay, start the Windows daemon
bridge/                Windows daemon + ros2 CLI + Ubuntu simulator + docs
gRPC/ros2_gRPC_sdk/    cross-machine SDK (proto + client/server libraries)
nav2/ros2_shim/        shared-memory ROS2 compatibility layer + the ported nav2
nav2/shm_transport/    shared-memory ring buffer (one algorithm, two platforms)
nav2/slam_toolbox_port/  Windows port of slam_toolbox
code/Desktop/racecar/  the car's mapping + navigation workspace (params, launch, waypoint scripts, maps)
tools/                 repo tooling: run tests, doc lint, structure check
package.py             build the release package
```

Every subdirectory has its own `README.md` explaining "why is this layer shaped like this".

## 5. Where to go next

| You want to | Go to |
| --- | --- |
| Understand how the three sub-projects fit | [Architecture](architecture.html) |
| Build / launch / stop things | [Build & Run](build-and-run.html) |
| Learn the `ros2` commands | [ros2 CLI](cli.html) |
| See what nav2 really reaches here | [Capabilities](capabilities.html) |
| Know what is **not** supported | [Limitations](limitations.html) |
| Reproduce the acceptance run | [Validation](validation.html) |
| Change the code | [Development](development.html) |
| Fix a failure | [Troubleshooting](troubleshooting.html) |
