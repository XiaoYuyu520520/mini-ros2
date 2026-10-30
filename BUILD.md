# 编译 · 启动 · 停止：Windows 与 Ubuntu 对照

按这个顺序读：**根目录 → 子项目 → 构建命令 → 兼容层环境 → 话题传输层 → nav2 / slam**。
命令一律**从项目根目录**执行；Linux 把 `python` 写成 `python3`、反斜杠换成 `/`。

## 1. 项目根目录

| | Windows | Ubuntu 设备 |
| --- | --- | --- |
| 项目根目录 | `E:\DesktopData\app\code\CPU`（`C:\Users\Administrator\Desktop\app\code\CPU` 是同一个目录，前者是它的链接） | `~/code/CPU`（= `/home/ubuntu/code/CPU`） |
| 进根目录 | `cd /d E:\DesktopData\app\code\CPU`（cmd）/ `cd E:\DesktopData\app\code\CPU`（PowerShell） | `cd ~/code/CPU` |
| Python | `E:\venvs\py312\Scripts\python.exe`，激活 `& "E:\venvs\py312\Scripts\Activate.ps1"` | `~/code/CPU/.venv/bin/python`（`python3` 也行） |
| 登录设备 | `ssh -i "C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519" ubuntu@192.168.1.113` | — |
| 编译工具链 | MSYS2 UCRT64（`tools\_toolchain.py` 解析，`CPU_MSYS2_ROOT` 覆盖）：gcc / cmake / ninja / gRPC / protobuf / yaml-cpp | 系统 apt 包，见 3.3 |
| 需要装 ROS2 吗 | 不需要 | 兼容层不需要；**`code/Desktop/racecar` 要** —— 它在 Ubuntu 上是
真 colcon 工作区，要 Jazzy 和几个 nav2 / slam_toolbox 的 apt 包，见 3.4 |

下面所有相对路径（`bridge/...`、`nav2/...`、`tools/...`）都相对这个根目录。

## 2. 有哪几个子项目、在哪

| 子项目 | 位置（相对根） | 角色 | 构建树 |
| --- | --- | --- | --- |
| ROS2 兼容层 + 移植的 nav2 | `nav2/ros2_shim/` | rclcpp / rclcpp_action / rcl 兼容头；话题走共享内存；nav2 的服务器、AMCL、BT 都从这里编 | `nav2/ros2_shim/build/` |
| 共享内存传输 | `nav2/shm_transport/` | 单写者多读者环形缓冲 + 反射注册表，两平台一份算法 | `nav2/shm_transport/build/` |
| slam_toolbox 移植 | `nav2/slam_toolbox_port/` | 上游源码在 `nav2/third_party/slam_toolbox`，本目录是移植胶水 | 无独立构建树，跟 `nav2/ros2_shim` 一起编 |
| 桥（胶水层） | `bridge/` | Windows 守护进程 + `ros2` CLI + 入口脚本（`.bat` / `.ps1` / `.sh` / 跨平台的 `.py` 启动器）+ 仿真 + 文档 | `bridge/windows/build/`（**两平台同名**） |
| 通信 SDK | `gRPC/ros2_gRPC_sdk/` | proto、C++ 客户端/服务端库、agent、示例 | `gRPC/ros2_gRPC_sdk/build/<平台键>/`（Windows `windows-x86_64`，Ubuntu `linux-x86_64`） |
| 仿真小车 + 雷达 + agent | `bridge/ubuntu/ros2_bridge_sim/` | Ubuntu 侧的仿真世界与 gRPC agent | 纯 Python，没有构建树 |
| **racecar 工作区（建图 + 导航）** | `code/Desktop/racecar/` | 那台小车的 ROS2 工作区（`src/racecar`、`src/slam_gmapping`、`src/lslidar_*`）。本仓库里这一份跑在兼容层上：入口 `tools/racecar_ctl.py`、参数 `src/racecar/config/`、地图 `map/`；`src/` 之外的真车专用件（底盘、编码器、IMU、相机、串口）已经移到 `code/Desktop/rubblish/` | Windows 上没有构建树（Python 入口 + 兼容层已有的二进制）；Ubuntu 上是真 colcon 工作区（`build/` `install/` `log/`），见 3.4 |
| 归档（`racecar` 里非建图 / 导航的部分，加上真车 `code/`） | `code/Desktop/rubblish/` | 底盘启动、串口 udev 规则、EKF / carto、摄像头、AI 推理那一整套，搬过去时一个字节没改，不参与构建；原路径对照表见 `code/Desktop/rubblish/README.md` | 无构建树 |

只读 / 支撑目录：`nav2/nav2_extracted/navigation2-1.5.1/`（上游 nav2 1.5.1，兼容层直接从这里编译，
**不要改**）、`nav2/patches/`（对上游改动的 patch + 构建期校验）、`nav2/third_party/`（上游包与解包产物）、
`tools/`（仓库级工具）、`bridge/docs/`（架构 / 运行 / 验收 / 缺口 / 审查）、`.scratch/`（实测证据）。

对应的测试树（一遍跑完五棵）：`python tools\run_ctest.py shim` / `shm` / `bridge` / `grpc` / `sim`
（Ubuntu 用 `python3`；`sim` 是 pytest，只在 Windows 跑）。

## 3. 构建命令：增量 / 全量 / 指定核心数

**增量**就是再跑一遍同一个入口（ninja 只重编改动过的）；**全量**在没有 `--clean` 的项目上是先删构建树；
**核心数**用 `--jobs N`。

### 3.1 Windows

| 子项目 | 增量 | 全量 | 指定核心数 |
| --- | --- | --- | --- |
| `nav2/ros2_shim` | `python nav2\ros2_shim\build.py` | 删 `nav2\ros2_shim\build` 再跑；BehaviorTree.CPP 也要重编就加 `--rebuild-btcpp` | `--jobs 6` |
| `nav2/shm_transport` | `python nav2\shm_transport\build.py` | 删 `nav2\shm_transport\build` 再跑（`--reconfigure` 只是重跑 cmake，不删产物） | `--jobs 6` |
| `bridge` | `python bridge\scripts\build_bridge.py` | `python bridge\scripts\build_bridge.py --clean` | `--jobs 6` |
| `gRPC/ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py` | `python gRPC\ros2_gRPC_sdk\scripts\build.py --clean` | `--jobs 6` |

`nav2/ros2_shim` 另有 `build.ps1`（同一件事的 PowerShell 外壳，干活的是 `build.py`），只编某个目标就把
目标名缀在命令后面。`shm_transport` 编完自动跑 ctest，`--no-test` 可跳过；bridge / gRPC 加 `--test` 会在
编完跑 ctest。

### 3.2 Ubuntu（同样的四个入口）

| 子项目 | 增量 | 全量 | 指定核心数 |
| --- | --- | --- | --- |
| `nav2/ros2_shim` | `python3 nav2/ros2_shim/build.py` | 删 `nav2/ros2_shim/build` 再跑（+ `--rebuild-btcpp`） | `--jobs 6` |
| `nav2/shm_transport` | `python3 nav2/shm_transport/build.py` | 删 `nav2/shm_transport/build` 再跑 | `--jobs 6` |
| `bridge` | `python3 bridge/scripts/build_bridge.py` | `python3 bridge/scripts/build_bridge.py --clean` | `--jobs 6` |
| `gRPC/ros2_gRPC_sdk` | `python3 gRPC/ros2_gRPC_sdk/scripts/build.py` | `python3 gRPC/ros2_gRPC_sdk/scripts/build.py --clean` | `--jobs 6` |

从零实测（2026-09-18）：`shim 14m44s → ctest 32/32`、`shm 2.9s → 1/1`、`bridge 59.3s → 6/6`、
`gRPC 14.4s → 4/4`。

### 3.3 关于核心数

四个入口的 `--jobs` **默认 6**，Windows 上还会把编译进程钉在 P0/P1/P10-P13（`tools\_cores.py` 检测，
那台机器 6 个 P 核 + 8 个 E 核）。`--jobs` 只改并发数，钉核照旧；要改钉核才用 `BIG6_CORES` /
`BIG6_NO_PIN`。Ubuntu 上钉核是 no-op，`--jobs 6` 只是并发上限 —— 那台是 8 核，6 是仓库策略而不是机器限制。
**不要所有核心一起编。**

Ubuntu 上第一次编译前装依赖：

```bash
sudo apt-get install -y build-essential cmake ninja-build zlib1g-dev \
  libeigen3-dev libboost-dev libboost-serialization-dev libboost-thread-dev \
  libboost-system-dev libboost-atomic-dev libboost-chrono-dev libboost-date-time-dev \
  libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc libyaml-cpp-dev
```

### 3.4 racecar 工作区：Ubuntu 上是标准 colcon 工作区，Windows 上没有构建树

`code/Desktop/racecar` 本身就是一个小车的 ROS2 工作区（`src/racecar` 是那个 ament_cmake 包，
`src/nav2_waypoint_cycle` 是小车自己的 Python 包，另有 `src/lslidar_*`、`src/slam_gmapping`、
`src/openslam_gmapping`）。

* **Windows**：没有构建树，也不该有 —— 这台机器上没有 ROS2，`src/racecar` 的节点和那些航点脚本
  全部经由兼容层跑（`racecar_ctl.py nav|slam|run`，见 6.5）。源码树就是源码树。
* **Ubuntu**：真 `colcon build`，`build/` `install/` `log/` 都在工作区根目录下；编完
  `source install/setup.bash`，包名就是 `racecar`（`ros2 pkg prefix racecar` 指到
  `install/racecar`）。

| | 在 `code/Desktop/racecar` 里跑 |
| --- | --- |
| 增量 | `colcon build --parallel-workers 6` |
| 全量（门禁跑的就是这条） | `rm -rf build install log && colcon build --parallel-workers 6` |
| 只编建图 / 导航要的两个包 | `colcon build --packages-select racecar nav2_waypoint_cycle --parallel-workers 6` |
| 指定核心数 | `--parallel-workers N`（仓库策略 6，理由同 3.3） |

那台设备第一次编这个工作区之前，先装 Jazzy 侧的依赖、把树推过去（`~/code/CPU` 里原来没有
`code/` 这棵树）：

```bash
sudo apt-get install -y ros-jazzy-nav2-amcl ros-jazzy-nav2-msgs \
  ros-jazzy-nav2-simple-commander ros-jazzy-nav2-bringup ros-jazzy-nav2-common \
  ros-jazzy-slam-toolbox
# 整树要编 lslidar_driver，它另需这四个：
sudo apt-get install -y libpcl-dev libpcap-dev \
  ros-jazzy-diagnostic-updater ros-jazzy-pcl-conversions ros-jazzy-pluginlib
python3 .scratch/push_racecar.py          # 从 Windows 跑：打包 → scp → 解到 ~/code/CPU/code/Desktop/racecar
source /opt/ros/jazzy/setup.bash && cd code/Desktop/racecar
colcon build --parallel-workers 6         # 整树：6 个包
```

实测（2026-09-19，`bridge/docs/03-validation.md` 证据 81）：干净重建 **6 个包 41.3 s**、
`CONCOLEXIT=0`；`install/racecar/share/racecar/` 下有 `config/ launch/ map/ maps/ rviz/`，
`install/racecar/lib/racecar/` 下是那 17 个航点脚本 + 4 条 CSV，`ros2 pkg executables racecar`
数得到 17 个（含带空格与中文名的）。

**为什么现在编整树，而且先 `rm -rf`**：整树以前编不过 —— `openslam_gmapping` 少了整个 `log/`
模块（`bridge/docs/04-gaps.md` 第二十八节第 17 条），2026-09-19 补上之后 6 个包全绿，
`lslidar_msgs` / `lslidar_driver` / `slam_gmapping` 这三个包第一次有了 Linux 上的编译结果。
`tools/check_cross_platform.py --only racecar-build` 的远端那一半跑的就是这条全量命令，
**先删再编是有意的**：增量构建会在「从零配置不过」的树上变绿，而那正是一个依赖没声明的包的样子。
编完它还会断言 17 个脚本、4 条航线 CSV 和 `config/nav.yaml` 都进了 `install/` 树 ——
`ros2 run racecar <脚本>` 读的是那棵树，不是源码树；本地那一半则是
`code/Desktop/racecar/tools/check_mission_scripts.py`（17 个脚本经 `shim_run.py` 逐个加载、
`waypoint_file()` 指向的文件都得在）。

## 4. ROS2 兼容层：加载环境变量

每个 `.py` 入口都是「编完就能跑」，但要让 `ros2` 命令和 nav2 的可执行文件在**这个窗口里**可用，
得先加载一次环境（`PATH`、共享内存几何、SDK 配置）。

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

`.bat` 改不了 PowerShell 会话（它跑在 cmd 子进程里，环境跟着子进程走），所以 PowerShell 里**必须点源
`.ps1`**；不想点源就 `bridge\windows\ros2_shell.bat`（开一个已经准备好的 PowerShell 窗口），或者一次
一条命令用 `bridge\windows\ros2.bat topic list -t`。

**Ubuntu（`source` 一次，本 shell 常驻）**

```bash
source ~/code/CPU/bridge/linux/bridge_env.sh
ros2 topic list -t
```

只敲一次、不碰当前 shell：`bash ~/code/CPU/bridge/linux/ros2.sh topic list -t`。

加载进去的是什么（两边同名同值）：

| 变量 | 作用 |
| --- | --- |
| `PATH` | 构建目录 + MSYS2 运行时（后者 Windows 专有，shim 的 `*.exe` 需要它；桥的守护进程与 `ros2.exe` 自带 DLL） |
| `NAV2_SHM_*` | 槽数 / 槽大小 / 大槽大小 / 图目录 / 命名空间 / 日志级别 |
| `ROS2_GRPC_SDK_CONFIG_DIR` | SDK 发现配置目录，默认 `gRPC/ros2_gRPC_sdk/config` |
| `ROS2_BRIDGE_*` | 连接目标、是否自动起守护进程、就绪文件与等待预算、日志路径与上限 |
| `NAV2_SHIM_BT_DIR` | 行为树 XML 目录，默认 `nav2/ros2_shim/config/bt` |

全部变量与默认值见 `bridge/docs/02-running.md` 第 10 节。环境是**临时**的：关窗口 / 退 shell 就恢复原样。

## 5. 话题传输层：Ubuntu 端起什么、Windows 端起什么、IP 在哪设

传输层 = gRPC + 共享内存：**Ubuntu 那台跑 agent**（服务端，把本地话题发出来、把收到的话转给本地），
**Windows 跑 `ros2_bridge_daemon.exe`**（客户端，连上去、把远端话题镜像进共享内存、把 Windows 发的
话题上行）。**先起 Ubuntu 侧，再起 Windows 侧**（顺序反了 Windows 会扫不到）。

### 5.1 Ubuntu 端启动

```bash
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh start     # 仿真 + agent，默认 0.0.0.0:50051
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh status    # pid + 端口
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh log 40    # 尾随 ~/sim.log
bash ~/code/CPU/bridge/scripts/ubuntu_sim_ctl.sh stop
```

`start` 认 `--port N`、`--goal X,Y`（不认识的参数原样转给 `run_sim`）。要交互看世界：

```bash
cd ~/code/CPU/bridge/ubuntu
PYTHONPATH=. ~/code/CPU/.venv/bin/python -m ros2_bridge_sim.run_sim --seed 7 --goal 6,-4
```

不想用仿真、要用那台机器上**真装的 ROS2**（Jazzy）当 peer：

```bash
bash bridge/scripts/ubuntu_ros2_setup.sh                      # 一次性：装 ROS2 jazzy（清华源优先）
bash bridge/scripts/ubuntu_ros2_peer.sh start                 # 起真 ROS2 图 + agent（--backend rclcpp）
bash bridge/scripts/ubuntu_ros2_peer.sh status                # 也可以 start / stop
```

`ubuntu_ros2_peer.sh` 起的是 `talker`（`/chatter`）、`add_two_ints_server`、`ros2_peer_service.py`
（`/probe_trigger`）、`fibonacci_action_server`，再加一个用真 rclcpp 后端的 `ros2_grpc_sdk_agent`
—— 那个 agent 才是 Windows 守护进程连的对象。从 Windows 上推过去跑：

```bat
python bridge\scripts\ubuntu_ssh.py --file bridge\scripts\ubuntu_ros2_setup.sh --quiet
python bridge\scripts\ubuntu_ssh.py --file bridge\scripts\ubuntu_ros2_peer.sh -- start
```

### 5.2 Windows 端启动

```bat
bridge\windows\ros2_bridge.bat start      :: 后台起守护进程并做 LAN 扫描
bridge\windows\ros2_bridge.bat run        :: 前台跑，Ctrl+C 停
bridge\windows\ros2_bridge.bat status     :: 图 + 日志路径
bridge\windows\ros2_bridge.bat log 40
bridge\windows\ros2_bridge.bat stop
```

也可以**不手工起**：`ros2.bat` / `ros2.ps1` 的第一条命令会自己拉起守护进程并等它完成第一次同步
（`wait_bridge_ready.bat` 轮询 `ROS2_BRIDGE_READY_FILE`）。不想自动起：`set ROS2_BRIDGE_AUTOSTART=0`。

镜像哪些话题由守护进程参数决定，默认就是 nav2 / SLAM 要的那几条：

```bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2_bridge.bat start --mirror /clock --mirror /odom --mirror /scan --mirror /tf --mirror /tf_static
```

### 5.3 Ubuntu 设备的 IP / 端口写在哪（具体文件）

| 要改什么 | 文件（行号） | 怎么改 |
| --- | --- | --- |
| 守护进程连谁（运行时） | 环境变量 `ROS2_BRIDGE_AGENT`（`bridge\windows\bridge_env.bat:14`、`bridge\linux\bridge_env.sh:13` 是注释里的用法） | `set ROS2_BRIDGE_AGENT=192.168.1.113:50051`，或 `ros2_bridge.bat start --agent 192.168.1.113:50051`。**不设就自动发现**（LAN 扫描） |
| ssh 目标机 | `bridge/scripts/ubuntu_ssh.py:32`（`DEFAULT_TARGET`）；`bridge/scripts/sync_to_ubuntu.py:38` `--host` 默认 `192.168.1.113`、`:46` `--agent-port` 默认 `50051` | 命令行覆盖 `--target ubuntu@IP` / `--host IP` |
| 跨平台门禁的目标机 | `tools/check_cross_platform.py:49`（`DEFAULT_TARGET`） | `--target ubuntu@IP`、`--key 密钥路径` |
| agent 绑定地址与扫描端口 | `gRPC/ros2_gRPC_sdk/config/ros2_gRPC_sdk.yaml`（`server.address: 0.0.0.0:50051`、`discovery.ports: [50051]`） | 改这个 yaml；默认端口常量在 `gRPC/ros2_gRPC_sdk/include/ros2_grpc_sdk/discovery/discovery.hpp` 的 `kDefaultBridgePort` |
| 仿真监听哪个地址 | `bridge/scripts/ubuntu_sim_ctl.sh:23`（`port=50051`）+ `run_sim --host/--port` | 从 Windows 传：`... -- start --port 50052` |

本机约定：设备 `192.168.1.113`，agent 端口 `50051`，密钥
`C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519`。

### 5.4 从 Windows 操作 Ubuntu 那台

```bat
python bridge\scripts\ubuntu_ssh.py "tail -n 20 ~/sim.log"                             :: 跑任意 shell
python bridge\scripts\ubuntu_ssh.py --file bridge\scripts\ubuntu_sim_ctl.sh -- start   :: 推脚本并执行
python bridge\scripts\sync_to_ubuntu.py --run                                          :: 同步仓库切片并启动
```

## 6. nav2 / slam 怎么启动（Windows 与 Ubuntu），加载哪里的 yaml

前置：守护进程在跑（5.2），而且**先复位小车再起栈**：

```bat
bridge\windows\ros2.bat service call /sim/reset std_srvs/srv/Trigger
```

顺序反了 `amcl_bridge_node` 会把偏掉的 `/odom` 当初始位姿，规划器对每个目标都报
`Start Coordinates ... was outside bounds`，不重启栈不会自愈。

### 6.1 启动命令

**首选 `bridge\scripts\` 下的 Python 入口**：Windows 和 Ubuntu 跑的是同一份脚本，Linux 上 `python` 写成
`python3`、反斜杠换 `/`。Windows 用 `cmd` 习惯的那些 `.bat` 都还在，是同一件事的 cmd 外壳。

| 想干什么 | 命令 | 说明 |
| --- | --- | --- |
| 真正的 nav2 服务器栈（`amcl_bridge_node` + planner + controller + bt_navigator + behavior + smoother + waypoint_follower + lifecycle_manager） | `python bridge\scripts\run_nav2_stack.py [--goal auto] [--map FILE] [--safety] [--duration 120]` | **推荐入口，Windows 与 Ubuntu 同一份**（Ubuntu 见 6.3）。`stop` 停机、`--help` 列全部开关；日志一节点一个文件（Windows `%TEMP%\nav2_stack\`、Ubuntu `/tmp/nav2_stack/`），「栈活了」看 `lifecycle_manager.log` 里的 `Managed nodes are active`。`.bat` 外壳：`bridge\windows\nav2_stack.bat`（参数一样）。**`--map FILE`** 拿保存好的图（PGM + YAML）当 `/map`：脚本先起 `map_server` 并把它推到 ACTIVE，再起 AMCL —— 和车上的 `Run_nav.launch.py map:=FILE` 是同一件事。`/map` 只许一个写者，所以守护进程要是正在镜像 `/map`，它会先拒绝启动、再打印把 `/map` 从镜像集里摘掉的那两条命令 |
| 建图（slam_toolbox） | `python bridge\scripts\run_slam_demo.py [--mode async] [--duration 60]` | **推荐入口，两平台同一份**。前台跑，`--duration` 到点自己退。默认发 `/map`，所以要让 SLAM 当唯一的图源时，守护进程别镜像 `/map`。`.bat` 外壳：`bridge\windows\slam_demo.bat` |
| nav2 demo：拿共享内存里的 `/map` + `/scan` 建代价地图、规划、开车到目标 | `python bridge\scripts\run_nav2_demo.py --goal auto` | **Windows 专用**（脚本里写死了 `.exe` / `tasklist` / `cmd`）。它自己导出 `bridge_env` 那套环境、确保守护进程、缺 `nav2_bridge_demo.exe` 时自动编（`--build-only` 只编，`--plan-only` 只规划，`--report x.json`） |
| 端到端验收（冷启动扫话题 → 上行 → nav2 demo → SLAM 一圈） | `python bridge\scripts\acceptance_check.py` | Windows 专用。`--quick` 只跑验收 1、2；`--skip-slam`；`--report out.json` |
| 定位（nav2_amcl）单起 | `python bridge\scripts\run_amcl_demo.py [--initial-pose X,Y,YAW]` | **两平台同一份**。栈里已经含 AMCL，一般不用单独起；默认拿 `/odom` 当初始位姿。`.bat` 外壳：`bridge\windows\amcl_demo.bat` |
| **racecar 的建图 / 导航 / 存图（推荐入口）** | `python code\Desktop\racecar\tools\racecar_ctl.py {nav\|slam\|save-map\|run\|stop\|status\|where\|mirror-all}` | 把下面几件事按顺序包好的那一条链，见 6.5。`--backend auto`（默认）看得见 `bridge/scripts/run_nav2_stack.py` 就用兼容层，否则走真车的 `ros2 launch`。`.sh` / `.bat` 外壳在 `code/Desktop/racecar/` 下：`nav.*`、`gmapping.*`、`save.*` |
| 安全链旁路 / docking + route | `python bridge\scripts\run_nav2_safety.py`、`python bridge\scripts\run_nav2_docking_route.py`（都认 `--duration S` / `--params FILE` / `stop`） | **两平台同一份**，可与栈同时跑。`.bat` 外壳：`bridge\windows\nav2_safety.bat`、`bridge\windows\nav2_docking_route.bat` |

### 6.2 它们加载哪里的 yaml

| 谁 | 加载什么 | 文件 |
| --- | --- | --- |
| nav2 栈（7 个服务器） | ① 上游主参数（`--params FILE` 可换）② 叠加层，**最后加载、覆盖上游** ③ `lifecycle_manager` 只读叠加层 | ① `nav2/nav2_extracted/navigation2-1.5.1/nav2_bringup/params/nav2_params.yaml` ② ③ `nav2/ros2_shim/config/nav2_shim_overrides.yaml` |
| 行为树 | `$(env NAV2_SHIM_BT_DIR)` 指向的 XML | `nav2/ros2_shim/config/bt/navigate_to_pose.xml`、`navigate_through_poses.xml` |
| `nav2_stack.bat --safety` | 加速度限制与停止多边形；`--safety-params FILE` 再叠最后一份 | `nav2/ros2_shim/config/nav2_shim_safety.yaml` + `nav2_shim_safety_chain.yaml` |
| `nav2_docking_route.bat` | docking / route 两个节点 | `nav2/ros2_shim/config/nav2_shim_docking_route.yaml` |
| `nav2_map_server` / `map_saver` | 地图存取：`--map FILE` 起的 `map_server` 在这里被调用（`yaml_filename` 是命令行给的），`save_map.py` 用的是同一个配置目录里的 `map_saver_cli` | `nav2/ros2_shim/config/nav2_map_server.yaml` |
| `run_nav2_stack.py --map FILE` | 就是 `map_server` 的 `yaml_filename`；放在 `--params-file` **之后**，因为 `nav2_shim_overrides.yaml` 里的 `map_server.yaml_filename: ''` 先说话就会赢 | 命令行里那一个文件 |
| **slam_toolbox** | **默认不读 yaml**：`slam_demo.bat` 只给 `-p base_frame:=base_link`；参数走 `NAV2_SLAM_PARAMS`，要文件就自己传 `--params-file`（传了就清掉默认 `-p`） | — |
| **nav2_amcl** | 同上：默认 `-p base_frame_id:=base_link -p laser_max_range:=12.0 …`；走 `NAV2_AMCL_PARAMS` 或 `--params-file` | — |
| nav2 demo（`nav2_bridge_demo.exe`） | 不读 yaml，参数在命令行（`--goal` / `--timeout` / `--report` / `--yaw` / `--frame`） | — |
| **racecar 的 nav2**（`racecar_ctl.py nav`） | 一份 `nav.yaml` 顶全部：AMCL、两个 costmap、规划器（Smac Hybrid + DUBIN）、控制器（RPP）、行为树路径、`lifecycle_manager` 的节点表 —— 所以入口带 `--no-overrides`，不再叠兼容层的默认值 | `code/Desktop/racecar/src/racecar/config/nav.yaml` |
| **racecar 的 slam_toolbox**（`racecar_ctl.py slam`） | `slam_toolbox.yaml`：上游取值 + 文件头逐条列出「兼容层 / 仿真专用」的那几项（`base_frame: base_link`、`max_laser_range: 12.0`、三个运动门槛 `0.1`） | `code/Desktop/racecar/src/racecar/config/slam_toolbox.yaml` |
| racecar 的地图 | `save-map -f NAME` 写、`nav --map NAME` 读（裸名字按 `map/` 解析；写 `live` 则先当场镜像仿真器的真值 `/map` 存成 `truth_live`，只有 shim 后端认） | `code/Desktop/racecar/map/NAME.{pgm,yaml}` |

换参数优先改 `nav2/ros2_shim/config/nav2_shim_overrides.yaml`（它本来就是干这个的）。**不要去改
`nav2/nav2_extracted/.../nav2_params.yaml`** —— 那是上游原样保留的副本，`nav2/patches/verify_patch.py`
和文档都以「上游只有一处例外」为前提。

### 6.3 Ubuntu 端怎么起 nav2 / slam

同一份脚本，`python` 写成 `python3`、反斜杠换成 `/`。**先起 Ubuntu 侧的仿真 + agent（5.1），再起栈**；
守护进程由 `wait_bridge_ready.sh` 自己拉起（`ROS2_BRIDGE_AGENT` 指到本机的 agent）：

```bash
cd ~/code/CPU
export ROS2_BRIDGE_AGENT=127.0.0.1:50051          # 仿真就在本机；不设就靠 LAN 自动发现

bash bridge/scripts/ubuntu_sim_ctl.sh start       # 5.1 的仿真 + agent
bash bridge/linux/ros2.sh service call /sim/reset std_srvs/srv/Trigger   # 起栈前先把小车放回原点

python3 bridge/scripts/run_nav2_stack.py --goal auto --report /tmp/goal.json
python3 bridge/scripts/run_slam_demo.py --duration 60
python3 bridge/scripts/run_nav2_stack.py stop
```

**为什么 Ubuntu 上也要守护进程**：仿真 `bridge/ubuntu/ros2_bridge_sim/` 的话题只活在它自己的进程里
（`.../ros2_bridge_sim/graph.py`），对外的唯一出口是 agent；守护进程把 agent 的话题镜像进本机的
共享内存图，nav2 才看得见 `/scan`、`/odom`、`/map`。`/cmd_vel` 反过来一样：栈写的 `/cmd_vel` 由守护
进程上行给 agent，小车才动。

实测（2026-09-18，设备 `ubuntu-ASUS`，8 核）：栈 25 s 报到 active，`--goal auto` **SUCCEEDED**
（`/cmd_vel` 119 条、`/plan` 6 条、从 (0,0) 走到 (2.35, 0.03)，2.35 m）；`--safety` 的两个节点也
推到 active；`run_slam_demo.py --duration 25` 一轮建图（`sync active: /scan -> /map, base_link frame`）。

### 6.4 停止

```bat
python bridge\scripts\run_nav2_stack.py stop
python bridge\scripts\run_nav2_safety.py stop
python bridge\scripts\run_nav2_docking_route.py stop
bridge\windows\nav2_stack.bat stop
bridge\windows\nav2_safety.bat stop
bridge\windows\nav2_docking_route.bat stop
bridge\windows\ros2_bridge.bat stop
taskkill /F /IM ros2.exe        :: 残留的发布者：ros2 topic pub 不带 --once / --duration 会一直发
```

Ubuntu 侧：`python3 bridge/scripts/run_nav2_stack.py stop`（按 `/proc/<pid>/cmdline` 找到本仓库
`nav2/ros2_shim/build/` 里的那些进程），`bash bridge/linux/ros2_bridge.sh stop` 停守护进程，
`bash bridge/scripts/ubuntu_sim_ctl.sh stop` 停仿真。关窗口即清环境。

### 6.5 racecar：建图 → 存图 → 导航，一条链

`code/Desktop/racecar/tools/racecar_ctl.py` 是这一整条链的推荐入口，Windows 与 Ubuntu 同一份
（Linux 上 `python` 写 `python3`、反斜杠换 `/`）：

| 想干什么 | 命令 |
| --- | --- |
| 这份工作区、入口、参数、地图都指到哪 | `python code\Desktop\racecar\tools\racecar_ctl.py where` |
| 什么在跑 | `... status`（按「还活着的进程」判，不会被残留的进程对象骗） |
| 让守护进程重新镜像全部话题（含 `/map`） | `... mirror-all` |
| 建图 | `... slam` —— 先把 `/map` 从镜像集里摘掉，让 SLAM 当唯一写者（`--duration N` 到点自退） |
| 存图 | `... save-map -f ai_map001` → `map/ai_map001.{pgm,yaml}` |
| 拿图导航 | `... nav --map ai_map001 --goal auto` |
| 让 `src/racecar/scripts/` 里的航点脚本直接跑在这台机器上 | `... run go.py sim_room12.csv`（shim 后端走 `nav2/ros2_shim/python/shim_run.py`，ros2 后端走 `ros2 run racecar <脚本>`） |
| 停车 | `... stop` |

起栈前先把小车放回原点（本节开头那条 `/sim/reset`），理由同 6 节开头。

实测（2026-09-18，Windows 侧 + Ubuntu 仿真 12 m 世界）：`slam` 走一条 4.5 m 见方的环线就建出整间房
（`233x233`、free 82.8% / unknown 14.9% / occupied 2.4%）；`nav --map slam_ship --goal auto` 拿这张
**SLAM 建的图**规划并驱动小车成功（`/plan` x8、`/cmd_vel` x77、1.63 m、`SUCCEEDED`）。完整数据与
「为什么运动门槛要改成 0.1」见 `bridge/docs/03-validation.md` 证据 66。

绕障也算过了（2026-09-18）：把一个**直线不通**的目标 `1.33,-4.83` 交给同一条链 —— 直线 5.01 m，
规划器给出来的是 8.07 m 的绕行路线（自己独立算的测地线 7.82 m），车实际走了 8.5~8.8 m、
最小离障 0.283 m、`/plan` x44、`/cmd_vel` x467、47.0 s、`SUCCEEDED`。见证据 67。

两条容易踩的坑（都是踩过才知道的）：

- **跑 `--map` 之前先停掉上一轮的栈**：`python bridge\scripts\run_nav2_stack.py stop`。
  上一轮留下的 `map_server.exe` 会继续占着 `/map` 的写者，新一轮起栈就卡在
  `[nav2_stack] could not drive /map_server/change_state transition 3`。
- **每趟 nav 之前先 `/sim/reset`**：车还停在上趟终点时，规划器给的 `remaining` 是 `0.00 m`、
  `/plan x0`，行为树直接报 `SUCCEEDED`，而 `travelled` 里那个数只是上一趟的位移 ——
  看着像过了，其实一步没走。

## 7. 哪些两平台都有，哪些还是 Windows 专属

| 东西 | Windows | Ubuntu | 说明 |
| --- | --- | --- | --- |
| `ros2` CLI（`topic` / `service` / `action` / `param` / `node` / `interface` / `bridge status`） | `bridge\windows\ros2.bat`，或 `ros2.ps1` 点源后直接 `ros2` | `bridge/linux/ros2.sh`，或 `source bridge/linux/bridge_env.sh` 后直接 `ros2` | 同名同语法，子命令与参数对齐 |
| 桥守护进程 | `bridge\windows\ros2_bridge.bat` | `bridge/linux/ros2_bridge.sh` | 同名子命令 `start` / `run` / `status` / `pid` / `stop` / `log` |
| nav2 服务器栈 | `run_nav2_stack.py` 或 `nav2_stack.bat` | `run_nav2_stack.py` | 同一份 Python；见 6.1 / 6.3 |
| slam_toolbox 建图 | `run_slam_demo.py` 或 `slam_demo.bat` | `run_slam_demo.py` | 同上 |
| 四棵构建树 + ctest | 3.1 | 3.2 | 同一套 `build.py` / `_toolchain.py` |
| nav2 演示 `nav2_bridge_demo` | `run_nav2_demo.py` | — | 脚本里写死 `.exe` / `tasklist` / `cmd`，还没跨平台 |
| 端到端验收 | `acceptance_check.py` | — | 同上 |
| racecar 建图 + 导航一条链 | `racecar_ctl.py`（或 `code\Desktop\racecar\` 下的 `nav.bat` / `gmapping.bat` / `save.bat`） | 同一份 `racecar_ctl.py`（`nav.sh` / `gmapping.sh` / `save.sh`） | 见 6.5；`--backend ros2` 时改走真车的 `ros2 launch` |
| AMCL 单起 / 安全链旁路 / docking+route | `run_amcl_demo.py`、`run_nav2_safety.py`、`run_nav2_docking_route.py`（或同名 `.bat`） | 同样的三份 `.py` | 同一份脚本 |

不是「能启动」就等于「功能齐」：slam_toolbox 与 nav2 还差哪些能力，逐条记在
`bridge/docs/04-gaps.md`（跨机编解码表之外的类型过不去、`/parameter_events` 跨机不通、
`docking` / `route` 没有 dock 实例与图文件、上游 `launch/` 那套语法本兼容层没有）。
