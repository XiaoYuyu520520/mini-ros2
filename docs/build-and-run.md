---
layout: default
title: 构建与运行 Build & Run
---

# 构建与运行 Build & Run

> **[中文](#中文) · [English](#english)**

---

# 中文

命令一律**从项目根目录**执行；Linux 把 `python` 写成 `python3`、反斜杠换成 `/`。

## 1. 项目根目录

| | Windows | Ubuntu 设备 |
| --- | --- | --- |
| 项目根目录 | `E:\DesktopData\app\code\CPU`（`C:\Users\Administrator\Desktop\app\code\CPU` 是同一个目录的链接） | `~/code/CPU` |
| 进根目录 | `cd E:\DesktopData\app\code\CPU` | `cd ~/code/CPU` |
| Python | `E:\venvs\py312\Scripts\python.exe`，激活 `& "E:\venvs\py312\Scripts\Activate.ps1"` | `~/code/CPU/.venv/bin/python` |
| 登录设备 | `ssh -i "C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519" wang@192.168.0.153` | — |
| 编译工具链 | MSYS2 UCRT64（`tools\_toolchain.py` 解析，`CPU_MSYS2_ROOT` 覆盖） | 系统 apt 包，见 §3.3 |

下面所有相对路径（`bridge/...`、`nav2/...`、`tools/...`）都相对这个根目录。

## 2. 有哪几个子项目、在哪

| 子项目 | 位置 | 角色 | 构建树 |
| --- | --- | --- | --- |
| ROS2 兼容层 + 移植的 nav2 | `nav2/ros2_shim/` | rclcpp / rclcpp_action 兼容头；话题走共享内存；nav2 的服务器、AMCL、BT 都从这里编 | `nav2/ros2_shim/build/` |
| 共享内存传输 | `nav2/shm_transport/` | 单写者多读者环形缓冲，两平台一份算法 | `nav2/shm_transport/build/` |
| slam_toolbox 移植 | `nav2/slam_toolbox_port/` | 移植胶水 | 无独立构建树，跟 `nav2/ros2_shim` 一起编 |
| 桥（胶水层） | `bridge/` | Windows 守护进程 + `ros2` CLI + 入口脚本 + 仿真 + 文档 | `bridge/windows/build/`（两平台同名） |
| 通信 SDK | `gRPC/ros2_gRPC_sdk/` | proto、C++ 客户端/服务端库、agent、示例 | `gRPC/ros2_gRPC_sdk/build/<平台键>/` |
| 仿真小车 + 雷达 + agent | `bridge/ubuntu/ros2_bridge_sim/` | Ubuntu 侧的仿真世界与 gRPC agent | 纯 Python，无构建树 |
| 中继（合并真 ROS2 与仿真） | `bridge/ubuntu/ros2_link/` | 两张图合并成一张给 Windows | 纯 Python |
| racecar 工作区 | `code/Desktop/racecar/` | 那台小车的 ROS2 工作区；Windows 上走兼容层，Ubuntu 上是真 colcon 工作区 | 见 §3.4 |

只读 / 支撑目录：`nav2/nav2_extracted/navigation2-1.5.1/`（上游 nav2 1.5.1，**不要改**）、
`nav2/patches/`（对上游改动的 patch + 构建期校验）、`nav2/third_party/`（上游包与解包产物）、
`tools/`（仓库级工具）、`bridge/docs/`（架构 / 运行 / 验收 / 缺口 / 审查）、`.scratch/`（实测证据）。

## 3. 构建命令

**增量**就是再跑一遍同一个入口（ninja 只重编改动过的）；**全量**是先删构建树；
**核心数**用 `--jobs N`。

### 3.1 Windows

| 子项目 | 增量 | 全量 | 指定核心数 |
| --- | --- | --- | --- |
| `nav2/ros2_shim` | `python nav2\ros2_shim\build.py` | 删 `nav2\ros2_shim\build` 再跑（BehaviorTree.CPP 也要重编就加 `--rebuild-btcpp`） | `--jobs 6` |
| `nav2/shm_transport` | `python nav2\shm_transport\build.py` | 删 `nav2\shm_transport\build` 再跑 | `--jobs 6` |
| `bridge` | `python bridge\scripts\build_bridge.py` | `python bridge\scripts\build_bridge.py --clean` | `--jobs 6` |
| `gRPC/ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py` | `... --clean` | `--jobs 6` |

`nav2/ros2_shim` 另有 `build.ps1`（同一件事的 PowerShell 外壳，干活的是 `build.py`），
只编某个目标就把目标名缀在命令后面。`shm_transport` 编完自动跑 ctest（`--no-test` 跳过）；
bridge / gRPC 加 `--test` 会在编完跑 ctest。

### 3.2 Ubuntu（同样的四个入口）

把 `python` 写成 `python3`、反斜杠换成 `/` 即可。从零实测（2026-09-18）：
`shim 14m44s → ctest 32/32`、`shm 2.9s → 1/1`、`bridge 59.3s → 6/6`、`gRPC 14.4s → 4/4`。

### 3.3 关于核心数

四个入口的 `--jobs` **默认 6**，Windows 上还会把编译进程钉在 P0/P1/P10-P13
（`tools\_cores.py` 检测；那台机器 6 个 P 核 + 8 个 E 核）。`--jobs` 只改并发数，钉核照旧；
要改钉核才用 `BIG6_CORES` / `BIG6_NO_PIN`。Ubuntu 上钉核是 no-op，`--jobs 6` 只是并发上限
（那台是 8 核，6 是仓库策略而不是机器限制）。**不要所有核心一起编。**

Ubuntu 上第一次编译前装依赖：

```bash
sudo apt-get install -y build-essential cmake ninja-build zlib1g-dev \
  libeigen3-dev libboost-dev libboost-serialization-dev libboost-thread-dev \
  libboost-system-dev libboost-atomic-dev libboost-chrono-dev libboost-date-time-dev \
  libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc libyaml-cpp-dev
```

### 3.4 racecar 工作区：Ubuntu 上是标准 colcon 工作区，Windows 上没有构建树

* **Windows**：没有构建树，也不该有 —— 这台机器上没有 ROS2，`src/racecar` 的节点和那些
  航点脚本全部经由兼容层跑（`racecar_ctl.py nav|slam|run`）。源码树就是源码树。
* **Ubuntu**：真 `colcon build`，`build/` `install/` `log/` 都在工作区根目录下。

| | 在 `code/Desktop/racecar` 里跑 |
| --- | --- |
| 增量 | `colcon build --parallel-workers 6` |
| 全量（门禁跑的就是这条） | `rm -rf build install log && colcon build --parallel-workers 6` |
| 只编建图 / 导航要的两个包 | `colcon build --packages-select racecar nav2_waypoint_cycle --parallel-workers 6` |

**为什么现在编整树，而且先 `rm -rf`**：增量构建会在「从零配置不过」的树上变绿，
而那正是一个依赖没声明的包的样子。

## 4. 加载环境变量

每个 `.py` 入口都是「编完就能跑」，但要让 `ros2` 命令和 nav2 的可执行文件在**这个窗口里**
可用，得先加载一次环境（`PATH`、共享内存几何、SDK 配置）。

**Windows · PowerShell（点源一次，本窗口常驻）**

```powershell
. E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1
ros2 topic list -t
```

**Windows · cmd**

```bat
call bridge\windows\bridge_env.bat
ros2 topic list -t
```

**Ubuntu**

```bash
source ~/code/CPU/bridge/linux/bridge_env.sh
ros2 topic list -t
```

只敲一次、不碰当前 shell：`bash ~/code/CPU/bridge/linux/ros2.sh topic list -t`。

> **`.bat` 改不了 PowerShell 会话**（它跑在 cmd 子进程里，环境跟着子进程走），所以
> PowerShell 里**必须点源 `.ps1`**。不想点源就 `bridge\windows\ros2_shell.bat`
> （开一个已经准备好的 PowerShell 窗口）。

加载进去的是什么（两边同名同值）：

| 变量 | 作用 |
| --- | --- |
| `PATH` | 构建目录 + MSYS2 运行时（Windows 专有） |
| `NAV2_SHM_*` | 槽数 / 槽大小 / 大槽大小 / 图目录 / 命名空间 / 日志级别 |
| `ROS2_GRPC_SDK_CONFIG_DIR` | SDK 发现配置目录，默认 `gRPC/ros2_gRPC_sdk/config` |
| `ROS2_BRIDGE_*` | 连接目标、是否自动起守护进程、就绪文件与等待预算、日志路径与上限 |
| `NAV2_SHIM_BT_DIR` | 行为树 XML 目录，默认 `nav2/ros2_shim/config/bt` |
| `ROS2_LINK_*` | 中继的三个端口与同名话题策略 |

**环境是临时的**：关窗口 / 退 shell 就恢复原样 —— 不写注册表、不改系统 PATH。

## 5. 话题传输层：谁起什么、IP 在哪设

**先起 Ubuntu 侧，再起 Windows 侧**（顺序反了 Windows 会扫不到）。

### 5.1 Ubuntu 端

```bash
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh start     # 仿真 + agent，默认 0.0.0.0:50051
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh status    # pid + 端口
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh log 40    # 尾随 ~/sim.log
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh stop
```

用那台机器上**真装的 ROS2**（Jazzy）当 peer：

```bash
bash bridge/scripts/ubuntu_ros2_setup.sh                      # 一次性：装 ROS2 jazzy
bash bridge/scripts/ubuntu_ros2_peer.sh start                 # 起真 ROS2 图 + agent
bash bridge/scripts/ubuntu_ros2_peer.sh status
```

起**中继**（把真 ROS2 图与仿真图合并成一张）：

```bash
bash start/connector.sh up -d          # 起中继（同时监听 0.0.0.0 与 127.0.0.1:50060）
bash start/connector.sh check          # 查两个上游 agent 是否活着
bash start/connector.sh status
```

### 5.2 Windows 端

```bat
bridge\windows\ros2_bridge.bat start      :: 后台起守护进程并做 LAN 扫描
bridge\windows\ros2_bridge.bat run        :: 前台跑，Ctrl+C 停
bridge\windows\ros2_bridge.bat status     :: 图 + 日志路径
bridge\windows\ros2_bridge.bat log 40
bridge\windows\ros2_bridge.bat stop
```

也可以**不手工起**：`ros2.bat` / `ros2.ps1` 的第一条命令会自己拉起守护进程并等它完成第一次
同步。不想自动起：`set ROS2_BRIDGE_AUTOSTART=0`。

镜像哪些话题由守护进程参数决定：

```bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2_bridge.bat start --mirror /clock --mirror /odom --mirror /scan --mirror /tf --mirror /tf_static
```

### 5.3 IP / 端口写在哪

| 要改什么 | 文件 | 怎么改 |
| --- | --- | --- |
| 守护进程连谁（运行时） | 环境变量 `ROS2_BRIDGE_AGENT` | `set ROS2_BRIDGE_AGENT=192.168.0.153:50060`，或 `ros2_bridge.bat start --agent host:port`。**不设就自动发现**（LAN 扫描） |
| ssh 目标机 | `bridge/scripts/ubuntu_ssh.py` 的 `DEFAULT_TARGET` | 命令行覆盖 `--target wang@IP` |
| agent 绑定地址与扫描端口 | `gRPC/ros2_gRPC_sdk/config/ros2_gRPC_sdk.yaml` | `server.address: 0.0.0.0:50051`、`discovery.ports: [50051]` |
| 中继的三个端口 | `ROS2_LINK_SIM_PORT` / `ROS2_LINK_REAL_PORT` / `ROS2_LINK_PORT` | 环境变量，默认 50051 / 50111 / 50060 |
| 同名话题谁保住原名 | `ROS2_LINK_COLLISION` | `prefer-sim`（默认）或 `prefer-real` |

## 6. nav2 / slam 怎么启动

前置：守护进程在跑，**而且先复位小车再起栈**：

```bat
bridge\windows\ros2.bat service call /sim/reset std_srvs/srv/Trigger
```

### 6.1 启动命令

**首选 `bridge\scripts\` 下的 Python 入口**：Windows 和 Ubuntu 跑的是同一份脚本。

| 想干什么 | 命令 |
| --- | --- |
| 真正的 nav2 服务器栈 | `python bridge\scripts\run_nav2_stack.py [--goal auto] [--map FILE] [--safety] [--duration 120]` |
| 建图（slam_toolbox） | `python bridge\scripts\run_slam_demo.py [--mode async] [--duration 60]` |
| nav2 demo（用共享内存里的 `/map` + `/scan` 建代价地图、规划、开车到目标） | `python bridge\scripts\run_nav2_demo.py --goal auto` |
| 端到端验收 | `python bridge\scripts\acceptance_check.py` |
| 定位（nav2_amcl）单起 | `python bridge\scripts\run_amcl_demo.py [--initial-pose X,Y,YAW]` |
| racecar 的建图 / 导航 / 存图 | `python code\Desktop\racecar\tools\racecar_ctl.py {nav\|slam\|save-map\|run\|stop\|status\|where\|mirror-all}` |
| 安全链旁路 / docking + route | `python bridge\scripts\run_nav2_safety.py`、`python bridge\scripts\run_nav2_docking_route.py` |

`.bat` 外壳：`bridge\windows\nav2_stack.bat` / `slam_demo.bat` / `amcl_demo.bat` /
`nav2_safety.bat` / `nav2_docking_route.bat`（参数与同名 `.py` 一一对应）。

**栈活了**看 `lifecycle_manager.log` 里的 `Managed nodes are active`（Windows `%TEMP%\nav2_stack\`，
Ubuntu `/tmp/nav2_stack/`）。

### 6.2 它们加载哪里的 yaml

| 谁 | 加载什么 | 文件 |
| --- | --- | --- |
| nav2 栈（7 个服务器） | ① 上游主参数 ② 叠加层（**最后加载、覆盖上游**）③ `lifecycle_manager` 只读叠加层 | ① `nav2/nav2_extracted/navigation2-1.5.1/nav2_bringup/params/nav2_params.yaml` ② ③ `nav2/ros2_shim/config/nav2_shim_overrides.yaml` |
| 行为树 | `$(env NAV2_SHIM_BT_DIR)` 指向的 XML | `nav2/ros2_shim/config/bt/navigate_to_pose.xml`、`navigate_through_poses.xml` |
| `nav2_stack.bat --safety` | 加速度限制与停止多边形 | `nav2/ros2_shim/config/nav2_shim_safety.yaml` + `nav2_shim_safety_chain.yaml` |
| docking / route | 两个节点 | `nav2/ros2_shim/config/nav2_shim_docking_route.yaml` |
| 地图存取 | `yaml_filename` 是命令行给的 | `nav2/ros2_shim/config/nav2_map_server.yaml` |
| racecar 的 nav2 | 一份 `nav.yaml` 顶全部 | `code/Desktop/racecar/src/racecar/config/nav.yaml` |
| racecar 的 slam_toolbox | 上游取值 + 逐条列出的兼容层专用项 | `code/Desktop/racecar/src/racecar/config/slam_toolbox.yaml` |

> 换参数优先改 `nav2/ros2_shim/config/nav2_shim_overrides.yaml`。**不要去改上游的
> `nav2_params.yaml`** —— 那是原样保留的副本，`nav2/patches/verify_patch.py` 和文档都以
> 「上游只有一处例外」为前提。

### 6.3 停止

```bat
python bridge\scripts\run_nav2_stack.py stop
python bridge\scripts\run_nav2_safety.py stop
python bridge\scripts\run_nav2_docking_route.py stop
bridge\windows\ros2_bridge.bat stop
taskkill /F /IM ros2.exe        :: 残留的发布者：ros2 topic pub 不带 --once 会一直发
```

Ubuntu 侧：`python3 bridge/scripts/run_nav2_stack.py stop`、
`bash bridge/linux/ros2_bridge.sh stop`、`bash bridge/scripts/ubuntu_sim_ctl.sh stop`。

## 7. 哪些两平台都有，哪些还是 Windows 专属

| 东西 | Windows | Ubuntu |
| --- | --- | --- |
| `ros2` CLI | `bridge\windows\ros2.bat`，或点源 `ros2.ps1` 后直接 `ros2` | `bridge/linux/ros2.sh`，或 `source bridge/linux/bridge_env.sh` 后直接 `ros2` |
| 桥守护进程 | `bridge\windows\ros2_bridge.bat` | `bridge/linux/ros2_bridge.sh` |
| nav2 服务器栈 | `run_nav2_stack.py` 或 `nav2_stack.bat` | `run_nav2_stack.py` |
| slam_toolbox 建图 | `run_slam_demo.py` 或 `slam_demo.bat` | `run_slam_demo.py` |
| 四棵构建树 + ctest | ✅ | ✅ |
| nav2 演示 `nav2_bridge_demo` | `run_nav2_demo.py` | — （脚本里写死 `.exe` / `tasklist` / `cmd`） |
| 端到端验收 | `acceptance_check.py` | — |
| racecar 建图 + 导航一条链 | `racecar_ctl.py` | 同一份 `racecar_ctl.py` |
| AMCL 单起 / 安全链 / docking+route | `run_amcl_demo.py` 等三份 `.py` | 同样的三份 `.py` |

不是「能启动」就等于「功能齐」：逐条缺口见 [限制与缺口](limitations.html)。

---

# English

Run all commands **from the project root**; on Linux write `python3` and use forward slashes.

## 1. Project root

| | Windows | Ubuntu device |
| --- | --- | --- |
| Project root | `E:\DesktopData\app\code\CPU` (`C:\Users\Administrator\Desktop\app\code\CPU` is a link to the same directory) | `~/code/CPU` |
| Python | `E:\venvs\py312\Scripts\python.exe`, activate with `& "E:\venvs\py312\Scripts\Activate.ps1"` | `~/code/CPU/.venv/bin/python` |
| Log in to the device | `ssh -i "C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519" wang@192.168.0.153` | — |
| Toolchain | MSYS2 UCRT64 (resolved by `tools/_toolchain.py`, override with `CPU_MSYS2_ROOT`) | system apt packages, see §3.3 |

## 2. Sub-projects

| Sub-project | Location | Role |
| --- | --- | --- |
| ROS2 compatibility layer + ported nav2 | `nav2/ros2_shim/` | rclcpp / rclcpp_action headers; topics over shared memory; all nav2 servers built here |
| Shared-memory transport | `nav2/shm_transport/` | Single-writer ring buffer, one algorithm for two platforms |
| slam_toolbox port | `nav2/slam_toolbox_port/` | Port glue (built together with `ros2_shim`) |
| Bridge (glue) | `bridge/` | Windows daemon, `ros2` CLI, entry scripts, simulator, docs |
| Communication SDK | `gRPC/ros2_gRPC_sdk/` | proto, C++ client/server libraries, agent, examples |
| Simulator + agent | `bridge/ubuntu/ros2_bridge_sim/` | Pure Python world and gRPC agent |
| Relay (merges real ROS2 and the simulator) | `bridge/ubuntu/ros2_link/` | Two graphs → one graph for Windows |
| racecar workspace | `code/Desktop/racecar/` | The car's ROS2 workspace; runs on the shim on Windows, real colcon on Ubuntu |

Read-only / support trees: `nav2/nav2_extracted/navigation2-1.5.1/` (upstream nav2 1.5.1,
**do not modify**), `nav2/patches/`, `nav2/third_party/`, `tools/`, `bridge/docs/`, `.scratch/`.

## 3. Building

**Incremental** = run the same entry point again (ninja only rebuilds what changed);
**full** = delete the build tree first; **core count** = `--jobs N`.

### 3.1 Windows

| Sub-project | Incremental | Full | Jobs |
| --- | --- | --- | --- |
| `nav2/ros2_shim` | `python nav2\ros2_shim\build.py` | delete `nav2\ros2_shim\build` first (add `--rebuild-btcpp` for BehaviorTree.CPP) | `--jobs 6` |
| `nav2/shm_transport` | `python nav2\shm_transport\build.py` | delete `nav2\shm_transport\build` first | `--jobs 6` |
| `bridge` | `python bridge\scripts\build_bridge.py` | `... --clean` | `--jobs 6` |
| `gRPC/ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py` | `... --clean` | `--jobs 6` |

`nav2/ros2_shim` also has a `build.ps1` shell (the real work is in `build.py`); append a target
name to build just that one. `shm_transport` runs ctest automatically after building
(`--no-test` to skip); bridge / gRPC run ctest with `--test`.

### 3.2 Ubuntu

Same four entry points with `python3` and forward slashes. Measured from scratch (2026-09-18):
`shim 14m44s → ctest 32/32`, `shm 2.9s → 1/1`, `bridge 59.3s → 6/6`, `gRPC 14.4s → 4/4`.

### 3.3 About core count

All four entry points default to `--jobs 6` and, on Windows, **pin** the compiler processes to
P0/P1/P10-P13 (`tools/_cores.py` detects; that machine has 6 P-cores + 8 E-cores). `--jobs`
only changes concurrency; use `BIG6_CORES` / `BIG6_NO_PIN` to change pinning. On Ubuntu pinning
is a no-op and `--jobs 6` is just an upper bound. **Do not build with every core.**

Install the dependencies on Ubuntu before the first build:

```bash
sudo apt-get install -y build-essential cmake ninja-build zlib1g-dev \
  libeigen3-dev libboost-dev libboost-serialization-dev libboost-thread-dev \
  libboost-system-dev libboost-atomic-dev libboost-chrono-dev libboost-date-time-dev \
  libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc libyaml-cpp-dev
```

### 3.4 The racecar workspace

* **Windows**: no build tree, and it should not have one — there is no ROS2 on that machine, so
  the `src/racecar` nodes and the waypoint scripts all run through the shim.
* **Ubuntu**: a real `colcon build`; `build/`, `install/`, `log/` live at the workspace root.

| | In `code/Desktop/racecar` |
| --- | --- |
| Incremental | `colcon build --parallel-workers 6` |
| Full (what the gate runs) | `rm -rf build install log && colcon build --parallel-workers 6` |
| Only the mapping / navigation packages | `colcon build --packages-select racecar nav2_waypoint_cycle --parallel-workers 6` |

**Why the full tree with `rm -rf` first**: an incremental build turns a tree that cannot configure
from scratch green — which is exactly what a package with an undeclared dependency looks like.

## 4. Loading the environment

```powershell
# Windows / PowerShell — dot-source once
. E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1
ros2 topic list -t
```

```bat
:: Windows / cmd
call bridge\windows\bridge_env.bat
ros2 topic list -t
```

```bash
# Ubuntu
source ~/code/CPU/bridge/linux/bridge_env.sh
ros2 topic list -t
```

One-shot without touching the current shell: `bash ~/code/CPU/bridge/linux/ros2.sh topic list -t`.

> **A `.bat` cannot change a PowerShell session** (it runs in a cmd child process, and the
> environment goes with it), so in PowerShell you **must dot-source the `.ps1`**. If you would
> rather not, `bridge\windows\ros2_shell.bat` opens a PowerShell window that is already prepared.

What gets loaded (same names and values on both platforms): `PATH`; `NAV2_SHM_*` (slot count /
sizes / graph dir / namespace / log level); `ROS2_GRPC_SDK_CONFIG_DIR`; `ROS2_BRIDGE_*` (target,
autostart, ready file and wait budget, log paths); `NAV2_SHIM_BT_DIR`; `ROS2_LINK_*` (the three
relay ports and the collision policy).

**The environment is temporary**: closing the window restores the original state — no registry
writes, no system PATH changes.

## 5. The transport layer: who starts what, and where the IP lives

**Start the Ubuntu side first**, then Windows (the other way round, Windows finds nothing).

```bash
# Ubuntu
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh start     # simulator + agent, default 0.0.0.0:50051
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh status
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh log 40
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh stop

# Use the machine's real ROS2 (Jazzy) as the peer instead
bash bridge/scripts/ubuntu_ros2_setup.sh
bash bridge/scripts/ubuntu_ros2_peer.sh start

# The relay (merges the real ROS2 graph and the simulator graph)
bash start/connector.sh up -d
bash start/connector.sh check
bash start/connector.sh status
```

```bat
:: Windows
bridge\windows\ros2_bridge.bat start      :: start the daemon in the background and sweep the LAN
bridge\windows\ros2_bridge.bat run        :: foreground, Ctrl+C to stop
bridge\windows\ros2_bridge.bat status
bridge\windows\ros2_bridge.bat log 40
bridge\windows\ros2_bridge.bat stop
```

You can also **not** start it by hand: the first `ros2.bat` / `ros2.ps1` command starts the daemon
on demand and waits for the first sync. To disable that: `set ROS2_BRIDGE_AUTOSTART=0`.

| To change | Where |
| --- | --- |
| Which agent the daemon connects to | env `ROS2_BRIDGE_AGENT`, or `ros2_bridge.bat start --agent host:port`. **Unset = auto-discover** via LAN sweep |
| The SSH target | `DEFAULT_TARGET` in `bridge/scripts/ubuntu_ssh.py`; override with `--target wang@IP` |
| Agent bind address and scan ports | `gRPC/ros2_gRPC_sdk/config/ros2_gRPC_sdk.yaml` |
| The three relay ports | `ROS2_LINK_SIM_PORT` / `ROS2_LINK_REAL_PORT` / `ROS2_LINK_PORT` (defaults 50051 / 50111 / 50060) |
| Who keeps the original name on a topic clash | `ROS2_LINK_COLLISION`: `prefer-sim` (default) or `prefer-real` |

## 6. Starting nav2 / SLAM

Prerequisite: the daemon is running, and **reset the car before starting the stack**:

```bat
bridge\windows\ros2.bat service call /sim/reset std_srvs/srv/Trigger
```

### 6.1 Commands

**Prefer the Python entry points in `bridge\scripts/`** — the same file runs on Windows and Ubuntu.

| Goal | Command |
| --- | --- |
| The real nav2 server stack | `python bridge\scripts\run_nav2_stack.py [--goal auto] [--map FILE] [--safety] [--duration 120]` |
| Mapping (slam_toolbox) | `python bridge\scripts\run_slam_demo.py [--mode async] [--duration 60]` |
| nav2 demo (costmap from `/map` + `/scan`, plan, drive) | `python bridge\scripts\run_nav2_demo.py --goal auto` |
| End-to-end acceptance | `python bridge\scripts\acceptance_check.py` |
| AMCL alone | `python bridge\scripts\run_amcl_demo.py [--initial-pose X,Y,YAW]` |
| racecar mapping / navigation / map saving | `python code\Desktop\racecar\tools\racecar_ctl.py {nav\|slam\|save-map\|run\|stop\|status\|where\|mirror-all}` |
| Safety chain (bypass) / docking + route | `python bridge\scripts\run_nav2_safety.py`, `python bridge\scripts\run_nav2_docking_route.py` |

`.bat` shells: `nav2_stack.bat`, `slam_demo.bat`, `amcl_demo.bat`, `nav2_safety.bat`,
`nav2_docking_route.bat` in `bridge\windows\` (same arguments as their `.py` twins).

**The stack is alive** when `lifecycle_manager.log` says `Managed nodes are active`
(Windows `%TEMP%\nav2_stack\`, Ubuntu `/tmp/nav2_stack/`).

### 6.2 Which YAML gets loaded

| Who | What | File |
| --- | --- | --- |
| The nav2 stack (7 servers) | ① upstream params ② the overlay (**loaded last, overrides upstream**) ③ `lifecycle_manager` reads only the overlay | ① `nav2/nav2_extracted/navigation2-1.5.1/nav2_bringup/params/nav2_params.yaml` ② ③ `nav2/ros2_shim/config/nav2_shim_overrides.yaml` |
| Behavior trees | the XMLs in `$(env NAV2_SHIM_BT_DIR)` | `nav2/ros2_shim/config/bt/navigate_to_pose.xml`, `navigate_through_poses.xml` |
| `nav2_stack.bat --safety` | acceleration limits and the stop polygon | `nav2/ros2_shim/config/nav2_shim_safety.yaml` + `nav2_shim_safety_chain.yaml` |
| docking / route | the two nodes | `nav2/ros2_shim/config/nav2_shim_docking_route.yaml` |
| racecar nav2 | one `nav.yaml` covers everything | `code/Desktop/racecar/src/racecar/config/nav.yaml` |
| racecar slam_toolbox | upstream values plus the shim-specific ones listed in the header | `code/Desktop/racecar/src/racecar/config/slam_toolbox.yaml` |

> Change parameters in `nav2/ros2_shim/config/nav2_shim_overrides.yaml` first. **Do not edit the
> upstream `nav2_params.yaml`** — it is kept verbatim, and `nav2/patches/verify_patch.py` and the
> docs both assume "upstream has exactly one exception".

### 6.3 Stopping

```bat
python bridge\scripts\run_nav2_stack.py stop
python bridge\scripts\run_nav2_safety.py stop
python bridge\scripts\run_nav2_docking_route.py stop
bridge\windows\ros2_bridge.bat stop
taskkill /F /IM ros2.exe        :: leftover publisher: ros2 topic pub without --once runs forever
```

On Ubuntu: `python3 bridge/scripts/run_nav2_stack.py stop`,
`bash bridge/linux/ros2_bridge.sh stop`, `bash bridge/scripts/ubuntu_sim_ctl.sh stop`.

## 7. Cross-platform vs Windows-only

| Thing | Windows | Ubuntu |
| --- | --- | --- |
| `ros2` CLI | `bridge\windows\ros2.bat`, or dot-source `ros2.ps1` | `bridge/linux/ros2.sh`, or `source bridge/linux/bridge_env.sh` |
| Bridge daemon | `bridge\windows\ros2_bridge.bat` | `bridge/linux/ros2_bridge.sh` |
| nav2 server stack | `run_nav2_stack.py` / `nav2_stack.bat` | `run_nav2_stack.py` |
| slam_toolbox mapping | `run_slam_demo.py` / `slam_demo.bat` | `run_slam_demo.py` |
| Four build trees + ctest | ✅ | ✅ |
| nav2 demo (`nav2_bridge_demo`) | `run_nav2_demo.py` | — (the script hardcodes `.exe` / `tasklist` / `cmd`) |
| End-to-end acceptance | `acceptance_check.py` | — |
| racecar mapping + navigation chain | `racecar_ctl.py` | same `racecar_ctl.py` |
| AMCL alone / safety chain / docking+route | three `.py` files | the same three |

"Starts successfully" is not "feature-complete": see [Limitations](limitations.html) for the itemised
gaps.
