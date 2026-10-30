# CPU · 在 Windows 上跑 nav2（gRPC 桥接 + 共享内存 ROS2 兼容层）

<p align="center">
  <b>🌐 在线文档站 / Documentation site：</b>
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/">https://xiaoyuyu520520.github.io/mini-ros2/</a>
</p>

<p align="center">
  <b>📖 在线文档 / Documentation：</b>
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/">https://xiaoyuyu520520.github.io/mini-ros2/</a>
</p>

<p align="center">
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/Quick_start/overview">1 快速开始</a> ·
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/Quick_start/windows">Windows 上手</a> ·
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/System_configuration/environment">2 系统配置</a> ·
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/Basic_application/ros2_cli">3 基础应用</a> ·
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/Architecture/overview">4 架构说明</a> ·
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/Capability/overview">5 能力清单</a> ·
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/FAQ/limitations">6 常见问题</a> ·
  <a href="https://xiaoyuyu520520.github.io/mini-ros2/Advanced/development">7 进阶开发</a>
</p>

<p align="center">
  <a href="https://github.com/XiaoYuyu520520/mini-ros2/releases"><img src="https://img.shields.io/github/v/release/XiaoYuyu520520/mini-ros2?label=release" alt="release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/XiaoYuyu520520/mini-ros2" alt="license"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Ubuntu-blue" alt="platform">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue" alt="c++17">
</p>

目标：Ubuntu 上的机器人（本仓库自带一个 Python 仿真小车 + 雷达）通过 gRPC 把话题送到
Windows，Windows 上的 **魔改 nav2** 建图、规划，再把 `/cmd_vel` 送回 Ubuntu，小车动起来 ——
也就是说，**在 Windows 上像在 Linux 上一样使用 ROS2**，不需要 DDS，也不需要安装 ROS2。

> 原始需求（保留）：
>
> 连接 ubuntu 设备命令 `ssh -i "C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519" wang@192.168.0.153`
>
> * `gRPC` 是一个基于 gRPC 的通信 sdk（初期开发，尚不稳定）
> * `nav2` 是一个正在开发的、基于共享内存的 ros2 兼容层，力求在 Windows 上面运行 nav2，
>   项目整体分为**兼容层 + 魔改适配的 nav2**
>
> 验收标注：
> 1. gRPC 通信 sdk + 基于共享内存的 ros2 兼容层（Windows），gRPC 通信 sdk 快速扫描建立持久连接后，
>    最后通过一个 bat 脚本运行临时加载环境变量后，就可以在 windows 平台使用 ros2 功能，
>    基础 `ros2 topic list` 等等保持一致；
> 2. 同时，Windows 发布话题，Ubuntu 端也可以正常订阅；
> 3. nav2 可以正常订阅话题建图，并规划路径，指挥小车正常移动。

**新手先看 [`GETTING_STARTED.md`](GETTING_STARTED.md)** ——五分钟说清它能干什么、能在什么机器上跑、怎么跑起来、不做什么。

**三条全部通过**，一键复现与实测数据见 [`bridge/docs/03-validation.md`](bridge/docs/03-validation.md)。

在这三条之上，nav2 那一侧现在还能做这些（都有一节证据，**未验证的部分也写在里面**）：

| 能力 | 状态 | 证据 |
| --- | --- | --- |
| 上游 nav2 服务器本体（AMCL + planner + controller + bt_navigator + behavior_server + smoother_server + waypoint_follower + lifecycle_manager）一条命令拉起并闭环 | 通过 | 证据 14 / 21 / 22 |
| 航点导航：`ComputePathToPose` → `SmoothPath` → `FollowWaypoints` 一条链（`nav2_waypoints.exe`） | 连跑 6 次全部 `SUCCEEDED`，`missed_waypoints` 全 0；最尖的拐角 45.0° → 4.9° | 证据 22 |
| 行为树末尾的恢复轮盘 `Spin` / `Wait` / `BackUp` | 真跑过一次，驱动了小车（倒车 0.31 m） | 证据 21 |
| 速度平滑 + 碰撞监控 | 旁路（`nav2_safety.bat`）：节点在跑、话题在流，**不改变车的行为**。进链（`nav2_stack.bat --safety`）：`collision_monitor` 成为 `/cmd_vel` 的**唯一写者**，停止多边形放大到 12 m 时车一步都不动（同一个目标：0.00 m / 2.55 m） | 证据 23 / 32 |
| 跨机 action / service 编解码 | **20 个 action（nav2 的 19 个 `.action` 全部）+ 38 个 service**；`spin` / `back_up` / `wait` / `set_initial_pose` / `enable` 端到端跑过（写这条时是 9 + 9） | 证据 24 / 61 |
| 局部控制器：DWB / MPPI / Graceful | 同一个栈里并存四个能被 `/controller_selector` 选中的控制器（`controller_plugins: ["FollowPath", "DWBFollowPath", "MPPIFollowPath", "GracefulFollowPath"]`，启动日志里四个都打印了 `Created controller`）、装载与激活都验过，都能在**运行期**切过去驱动小车（MPPI 是上游默认的那个）；`rotation_shim` 也编好了，但它占的就是 `FollowPath` 这个 id（换的是 `plugin`），要用自己那份参数文件起栈，默认参数下一处掉头场景会卡死 | 证据 25 / 27 / 30 / 34 |
| 地图存取：PGM + 8 位 PNG | `map_saver_cli.exe` 存 PGM / PNG + YAML，`map_server.exe` 读回来；PNG 让 `mode: scale` 的透明通道存得下来，`slam_toolbox` 默认配置里的 `image_format: png` 也能直接用（替身认 8 位非交错、五种颜色类型、五种行过滤器，压缩用工具链自带的 zlib，`-static` 跟着进 exe） | 证据 15 / 35 / 36 |
| 活的 SLAM 一整圈：`/scan` 进桥 → `slam_toolbox` 建图 → 三种存档 → `map_server` 读回来 | 车走一条闭合的圈，八个 action 全 `SUCCEEDED`；`473 x 473` 的图三种拿法像素一致，经 `map_server` 往返与源文件 **0 处不同** | 证据 36 |
| 参数事件流：`/parameter_events` | 节点自己发（`declare_parameter` → `new_parameters`，应用成功的 `set` → `changed_parameters`），另一个进程不用事先知道任何服务名就能看参数变化；整条栈起来时图里 14 个 publisher，多个进程共用一个环形缓冲。**跨机不通**（编解码表里没有这个类型，`ros2 topic echo` 只打得出字节数），所以另带一个用 shim 自己解码的 `parameter_events_probe.exe` | 证据 39 |
| 运行期调参：参数服务 + `ros2 param` | 节点自己名字下的六个参数服务，`list` / `get` / `set` / `describe` / `dump` 对着**正在跑**的节点读写（`set` 会走 nav2 自己的校验回调，非法值被拒且节点日志留痕）；`ros2 param list` 的节点名与上游一样可省，不写就遍历全图 | 证据 33 / 37 |
| `vector_object_server.exe` | 被生命周期管理器推到 ACTIVE、发出 `/vo_map`（`nav_msgs/msg/OccupancyGrid`）之后干净退出 | 证据 34 |
| 自主回充 / 路网（`nav2_docking_route.bat`） | 两个节点都能拉到 ACTIVE、插件真的被建出来；**没有 dock 实例、没有图文件**，所以只到「装得起来」 | 证据 28 |

## 目录结构

顶层就这几样。`C:\Users\Administrator\Desktop\app\code\CPU` 和
`E:\DesktopData\app\code\CPU` 是同一个目录（前者是链接），两种写法都能用：

```
CPU/
├─ README.md                本文件：目标、结构、怎么用、怎么开发
├─ GETTING_STARTED.md       新手入口：五分钟说清能干什么、能在哪跑、怎么跑起来、不做什么
├─ BUILD.md                 编译 / 启动 / 停止速查：Windows 与 Linux 各一块，逐项目写清
│                           进哪个目录、敲什么命令、怎么算编过了、怎么加载环境、怎么停
├─ docs/                    ★ 文档站（GitHub Pages 的源）：按章节组织的中英双语文档
│                           Quick_start / System_configuration / Basic_application /
│                           Architecture / Capability / FAQ / Advanced
├─ bridge/                  ★ 胶水层：Windows 守护进程 + ros2 CLI + Ubuntu 仿真 + 文档与验收
├─ gRPC/ros2_gRPC_sdk/      ★ 通信 SDK：proto、客户端/服务端库、示例、自带 ctest
├─ code/Desktop/racecar/    小车的建图（SLAM）+ 导航（nav2）工作区：参数、launch、
│                            航点脚本、地图、入口 `tools/racecar_ctl.py`
├─ code/Desktop/rubblish/    从 racecar 里搬出去的非建图 / 导航部分（底盘启动、
│                            串口、EKF、摄像头、AI 推理），原样保留、不参与构建
├─ nav2/                    ★ Windows 上的 ROS2 兼容层 + 移植进来的 nav2 / slam_toolbox
├─ tools/                   仓库级开发工具（跑测试、压测、文档体检、证据引用扫描）
├─ .scratch/                历次迭代的实测证据与一次性探针（规矩见 .scratch/README.md）
├─ .gitignore               忽略规则：build/ 产物、__pycache__、.scratch/attic、上游 zip
└─ .gitattributes           换行约定（`* text=auto eol=lf`；`.bat` / `.ps1` 是 CRLF）——
                            `tools/check_docs.py` 对 `.md` 里任何一个 CR 字节都报错
```

`code/` 是真车工作区搬进来的地方，`code/Desktop/` 下只留**建图（SLAM）和
**导航（nav2）**。不属于这两件事的都在 `code/Desktop/rubblish/`：`car.sh` /
`racecar_init.sh`（底盘启动）、串口 udev 规则、EKF / carto、`racecar_driver`、摄像头、
AI 推理。搬过去时一个字节没改，也不参与构建；原路径对照表和退回办法在
`code/Desktop/rubblish/README.md`。

三个 ★ 子项目各自的骨架：

```
bridge/
├─ windows/                     Windows 侧：守护进程 + `ros2` CLI + bat + ctest
│  ├─ src/bridge_daemon.cpp     守护进程：LAN 快速扫描、持久 gRPC 连接、双向镜像、图维护
│  ├─ src/type_registry.cpp     CDR ↔ 共享内存转码 + 跨机类型表（编/解码 + 表项三处）
│  ├─ src/service_registry.cpp  跨机 service 转发；action_registry.cpp 同理，管 action
│  ├─ include/ros2_bridge/      上面这些的头文件（cdr_archive / shm_topic / transcode …）
│  ├─ tools/ros2_cli.cpp        `ros2` 命令本体：topic / service / param / action / node
│  ├─ tests/                    3 条 ctest：cdr_check / service_roundtrip / action_roundtrip
│  ├─ cmake/bundle_runtime.cmake 把 MSYS2 的 DLL 收集到 build/ 旁边，好让 exe 单独跑
│  ├─ *.bat                     10 个入口，见下面「Windows 侧入口一览」
│  └─ build/                    构建产物（exe / DLL / CTestTestfile.cmake）
├─ ubuntu/ros2_bridge_sim/      Ubuntu 侧：纯 Python 的 gRPC agent + 仿真小车
│  ├─ world.py / sim.py         仿真世界（房间、墙壁、激光模型）与小车运动学
│  ├─ messages.py               话题表 + CDR 编解码；services.py 管服务与动作
│  ├─ graph.py                  机器人侧的图注册表（谁发谁收）
│  ├─ agent.py                  gRPC 服务端，把上面这些接到桥上
│  ├─ run_sim.py                启动入口，带一个交互控制台
│  └─ tests/                    61 条 pytest（在 Windows 上跑，见下面）
├─ scripts/                     跨机运维脚本，见下面「仓库脚本一览」
├─ tests/fixtures/              用 rosbags 生成的 CDR 基准字节 + manifest.tsv
└─ docs/                        01 架构 / 02 运行 / 03 验证 / 04 缺口 / 05 审计
                                + validation/（验收报告 json 与地图 png）
```

```
gRPC/ros2_gRPC_sdk/
├─ proto/ros2_bridge.proto      服务定义：Ping / ListTopics / GetTopicInfo / Subscribe / Publish
├─ include/ros2_grpc_sdk/       client / server / common / discovery 四组头文件
├─ src/                         对应实现；服务端既能跑 rclcpp 后端，也能跑 mock
├─ examples/                    6 个示例程序：mock_agent / publish / subscribe / discover /
│                               list_topics / bridge_demo
├─ tests/                       4 条 ctest：localhost_e2e / services / config / discovery
├─ config/ros2_gRPC_sdk.yaml    端口、超时、发现范围等运行期配置
├─ scripts/                     build.py（构建入口）、discover.py、run_local_demo.py、
│                               config.py、platform_utils.py
└─ build/                       windows-x86_64 构建产物与生成的 gRPC 桩
```

```
nav2/
├─ ros2_shim/                   共享内存 ROS2 兼容层 + 移植进来的 nav2
│  ├─ include/ros2_shim/        rclcpp / rclcpp_action / rcl / 内置消息的兼容头 + prelude.hpp
│  ├─ src/                      兼容层实现 + params_file.cpp + btcpp_groot_stub.cpp
│  ├─ apps/*_node.cpp           每个 nav2 服务器的 main（planner / controller / bt_navigator …）
│  ├─ tools/generate_messages.py 从 .msg/.srv/.action 生成 C++ 结构体与 Python 包
│  ├─ generate.ps1              重新生成消息的入口（把各个 --root 顺序定死）
│  ├─ generated/                生成物，不要手改：include/ 头文件 + python/pymessages/
│  ├─ generated_nav2/           遮蔽个别上游头文件用的生成头（例如 nav2_behavior_tree）
│  ├─ config/                   nav2 参数文件（含 safety / docking_route / map_server / bt）
│  ├─ tests/                    31 条 ctest 的源码 + 几个现场探针（probe 不注册成 ctest）
│  ├─ CMakeLists.txt            一棵树构建全部；末尾自动给每条测试注入 NAV2_SHM_NAMESPACE
│  └─ build.py / build.ps1      构建入口；.ps1 只是外壳，干活的是 build.py（只用 6 个大核）
├─ shm_transport/               共享内存传输：通道环形缓冲 + 反射注册表（独立 ctest 1 条，
│                               Windows + Linux 两套后端共用一份算法）
├─ slam_toolbox_port/           上游 slam_toolbox 的 Windows 移植（4 条 ctest）
├─ third_party/                 上游源码与解包产物（slam_toolbox、common_interfaces、
│                               geometry2、ompl、robot_localization …）
├─ nav2_extracted/navigation2-1.5.1/   navigation2 1.5.1 的源码，兼容层**直接从这里编译**
├─ patches/                     对上游改动的两份最小 patch + verify_patch.py（构建期校验）
└─ navigation2-1.5.1.zip        原始发行包（离线重建用，已进 .gitignore）
```

**一棵树一张 README**：下面每个有自己规矩的目录都带一份 README —— `bridge/windows`、
`bridge/linux`、`bridge/tests`、`bridge/docs`、`bridge/scripts`、`nav2/ros2_shim`（含它的
`python/`）、`nav2/shm_transport`、`nav2/slam_toolbox_port`、`nav2/third_party`，改哪个先读
哪个。它们写的是「这一层为什么长这样、什么能改什么别动」，根 README 只负责把人领到门口。
要确认这套东西没漂（README 齐不齐、上面几张结构图里的目录还在不在、文档点名的入口文件还
在不在），跑 `python tools\check_structure.py`，三条契约全绿才算数。

想从代码入手，三条最短路径：

* **桥怎么走**：`bridge/windows/src/bridge_daemon.cpp` → `bridge/docs/01-architecture.md`
* **兼容层怎么骗过 nav2**：`nav2/ros2_shim/include/ros2_shim/prelude.hpp`（每个 TU 都被
  `-include` 注入）→ `nav2/ros2_shim/CMakeLists.txt:96` 起的 `nav2_package_sources()`
* **仿真小车怎么动**：`bridge/ubuntu/ros2_bridge_sim/sim.py` → `world.py`

## 三个子项目

| 路径 | 角色 | 说明 |
| --- | --- | --- |
| `bridge/` | 本次新增的胶水层 | Windows 守护进程（发现 + 持久 gRPC 连接 + 双向转码）、`ros2` CLI、Ubuntu 仿真小车、bat 入口、文档与验收 |
| `gRPC/ros2_gRPC_sdk` | 通信 SDK | 话题传输用 gRPC；`proto/ros2_bridge.proto` 定义了 `Ping` / `ListTopics` / `GetTopicInfo` / `Subscribe`（流）/ `Publish` |
| `nav2/ros2_shim` | 共享内存 ROS2 兼容层 | 在 Windows 上提供 rclcpp/rclcpp_action/rcl 等接口，话题走 `ros2shim<topic>` 共享内存通道（`NAV2_SHM_NAMESPACE` 可加前缀隔离两套栈），图注册表代替 DDS 发现；构建入口 `python nav2\ros2_shim\build.py`（或同目录的 `build.ps1`），两者都只用 6 个大核 |
| `nav2/nav2_extracted` | 魔改前的 nav2 源码 | `navigation2-1.5.1` 保留原样，**只有一处例外**（`nav2_mppi_controller/tools/parameters_handler.hpp` 两行改成 `value_to_string()`，见证据 30）；`nav2_amcl` 等包从这里移植到兼容层 |
| `nav2/third_party/slam_toolbox` | 上游 slam_toolbox | 2.8.5，连同 `nav2/slam_toolbox_port` 一起移植成 Windows 静态 exe。**对它的四处改动有单独的 patch 和构建期检查**：`nav2/patches/` |

数据流：

```
Ubuntu（仿真小车）                                   Windows
/scan /odom /map /tf /clock  --gRPC-->  ros2_bridge_daemon.exe  --共享内存-->
                                                              nav2 规划 / ros2 CLI / 你的节点
小车移动  <--gRPC--  ros2_bridge_daemon.exe  <--共享内存--  /cmd_vel
```

## 怎么使用

下面 8 步是照着敲就能跑通的完整路径：第 1 步构建 Windows 侧，第 2 步把仿真推到
Ubuntu 并启动，第 3、4 步分别对应验收标准 1 和 3，5~8 步是 nav2 的完整能力与验收标准 2。

### 首次跑通

```bat
:: C:\Users\Administrator\Desktop\app\code\CPU 是同一个目录的链接，这里用真身
cd E:\DesktopData\app\code\CPU

:: 1) 构建 Windows 侧（守护进程 + ros2 CLI），并跑转码自检
E:\venvs\py312\Scripts\python.exe bridge\scripts\build_bridge.py --test

:: 2) 把 Ubuntu 侧推过去并启动仿真小车
E:\venvs\py312\Scripts\python.exe bridge\scripts\sync_to_ubuntu.py --run

:: 3) 在 Windows 上用 ROS2（首次会自动扫描、建连、镜像远端话题）
bridge\windows\ros2.bat topic list -t

:: 4) nav2 建图 + 规划 + 指挥小车
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_nav2_demo.py --goal auto

:: 5) 上游 nav2 服务器本体：先把小车放回原点，再起栈并挑一个可达目标开过去
::    （Windows 与 Ubuntu 跑同一份 `run_nav2_stack.py`；cmd 习惯的等价物是 nav2_stack.bat）
bridge\windows\ros2.bat service call /sim/reset std_srvs/srv/Trigger
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_nav2_stack.py --goal auto --report goal.json
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_nav2_stack.py stop
:: 5a) 拿存好的图（PGM + YAML）导航：脚本先起 map_server 再起 AMCL，等价于车上的 Run_nav.launch.py map:=FILE
::     注意 /map 只许一个写者 —— 守护进程正在镜像 /map 时它会拒绝启动，并打印把 /map 摘出镜像集的那两条命令
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_nav2_stack.py --map code\Desktop\racecar\map\ai_map001.yaml --goal 1.54,4.23 --timeout 90

:: 5b) 运行期换控制器（FollowPath=RPP / DWBFollowPath / MPPIFollowPath / GracefulFollowPath，栈不必重启）
bridge\windows\ros2.bat topic pub /controller_selector std_msgs/msg/String data=MPPIFollowPath --once
::     取值也认真 ROS2 那份 YAML（引号是给 shell 的），流式与块式缩进都算，两种写法可以混用：
bridge\windows\ros2.bat topic pub /controller_selector std_msgs/msg/String "{data: MPPIFollowPath}" -1
bridge\windows\ros2.bat action send_goal /sim/spin nav2_msgs/action/Spin "{target_yaw: 1.5}" -t 25
:: 5c) 对着正在跑的节点调参（参数服务是本机的，不经过桥）
bridge\windows\ros2.bat param list /controller_server -t
bridge\windows\ros2.bat param set /controller_server min_x_velocity_threshold 0.5

:: 5c-2) 另一个窗口盯着参数事件流：节点自己发的话题，不需要服务名
nav2\ros2_shim\build\parameter_events_probe.exe --count 5 --timeout 60

:: 5d) 地图存档：PGM 或 8 位 PNG（scale 模式要 PNG 才带透明通道）
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt png

:: 6) 旁路的安全链（速度平滑 + 碰撞监控），可以和上面的栈同时跑
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_nav2_safety.py

:: 6b) 进链：控制器改发 /cmd_vel_nav，小车听的是 collision_monitor 的输出
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_nav2_stack.py --safety --goal auto

:: 6c) 定位：AMCL 单独起（栈里已经含 AMCL，一般不单起）
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_amcl_demo.py --duration 60

:: 7) 航点链（规划 → 平滑 → 巡航）与旁路的 docking / route（栈先起来）
nav2\ros2_shim\build\nav2_waypoints.exe --goal auto --count 3
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_nav2_docking_route.py --duration 60

:: 8) 四条验收标准（9 条检查）一次跑完
E:\venvs\py312\Scripts\python.exe bridge\scripts\acceptance_check.py
```

### PowerShell 里日常用（点源一次）

`cmd` 里每条命令都自带环境：`bridge\windows\ros2.bat topic list -t` 一次调用就把守护进程和环境都
准备好。PowerShell 不行 —— `.bat` 里的 `set` 只作用于它自己那个 `cmd` 子进程，子进程一退环境就跟着走
了，所以你直接跑 `ros2.bat`（不带参数）只会看到用法说明，窗口里什么都不会留下。

PowerShell 侧的对应入口是 `bridge\windows\ros2.ps1`，**点源一次**就够：

```powershell
# 每个窗口点源一次，之后这个窗口里就不用再带路径
. E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1

ros2 topic list -t
ros2 topic echo /scan --count 3
ros2 bridge status
```

* 点源只**导入环境变量**（`bridge_env.bat` 仍是唯一事实源，这里不重复任何取值），不会启动守护进程；
  第一条 `ros2` 命令才会按需启动。冷启动实测 1.6~2.7 s（扫描 → 握手 → 建立持久通道 → 镜像远端话题，大头是 LAN 扫描）。
* 一行跑完也行，不用点源：`& E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1 topic list -t`。
* 习惯敲 bat、又想拿到上面这套环境：`bridge\windows\ros2_shell.bat`（可以带一个起始目录）会开一个新的 PowerShell 窗口，`ros2.ps1` 已经在里面点源好了。子进程改不了你当前会话的变量，所以「bat 一键拿到完整环境」只有开新窗口这一条路。
* 改过 `bridge_env.bat` 之后，用 `Import-Ros2Env -Force` 在本窗口重读一次。
* PowerShell profile 里**没有**任何 ros2 相关的东西：新窗口要么点源一次 `ros2.ps1`，要么用
  `ros2_shell.bat` 开一个已经点源好的窗口。别的窗口里 `ros2` 就是「不认识这个命令」。

### Linux 上日常用（`bridge/linux`）

Linux 侧是四个 `.sh`，和 `bridge\windows` 的四个入口一对一，一律用 `bash` 调用（仓库里的 `.sh`
不带头执行位）：

| 入口 | 对应 | 作用 |
| --- | --- | --- |
| `bridge/linux/bridge_env.sh` | `bridge_env.bat` | **`source` 一次**，环境就留在这个 shell 里，连 `ros2` 一起进 PATH |
| `bridge/linux/ros2.sh` | `ros2.bat` | 一条命令用 ROS2：确保守护进程在跑、等首次同步、再把参数转给 CLI |
| `bridge/linux/ros2_bridge.sh` | `ros2_bridge.bat` | 守护进程控制：`start` / `run` / `status` / `pid` / `stop` / `log [N]` |
| `bridge/linux/wait_bridge_ready.sh` | `wait_bridge_ready.bat` | 起守护进程并等 `ROS2_BRIDGE_READY_FILE` |

```bash
bash bridge/linux/ros2.sh topic list -t      # 一次性用法，不碰当前 shell

source bridge/linux/bridge_env.sh            # 或者在本 shell 里常驻
ros2 topic list -t
```

两点和 Windows 不一样。守护进程要有自己的会话才活得比调用它的 shell 久，所以 `start` 走 `setsid`，
pid 记在 `$ROS2_BRIDGE_BUILD/ros2_bridge_daemon.pid`；**发现不看本机**（`discovery.cpp` 故意把本机
所有地址排除在扫描之外），所以守护进程和 agent 跑在同一台机器上时必须给地址：
`ROS2_BRIDGE_AGENT=127.0.0.1:50051 bash bridge/linux/ros2_bridge.sh start`。跨机（Windows ↔ Ubuntu）
不需要这一条。

### Windows 侧入口一览（都在 `bridge\windows\`）

| 入口 | 作用 | 例子 |
| --- | --- | --- |
| `bridge_env.bat` | 只设环境变量（PATH、共享内存几何、SDK 配置、日志级别），供别的脚本 `call`；自己不开窗口 | `call bridge\windows\bridge_env.bat` |
| `ros2.bat` | Windows 上的 `ros2` 命令。首次调用会自动做 LAN 扫描、建立持久连接、把远端话题镜像进共享内存，之后复用已经跑着的守护进程 | `bridge\windows\ros2.bat topic list -t` |
| `ros2.ps1` | 同一件事的 PowerShell 版本。**点源一次**，本窗口里直接敲 `ros2 topic list -t`；点源只导入环境变量，守护进程仍然按需启动 | `. E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1` 然后 `ros2 topic list -t` |
| `ros2_shell.bat` | 「bat 找 ps1」：开一个**已经点源好 `ros2.ps1`** 的新 PowerShell 窗口，变量和 `ros2` 函数都在里面；守护进程仍然按需启动 | `ros2_shell.bat` 或 `ros2_shell.bat D:\work` |
| `ros2_bridge.bat` | 守护进程的 `start` / `run` / `status` / `stop` | `bridge\windows\ros2_bridge.bat status` |
| `wait_bridge_ready.bat` | 轮询 ready 文件，等守护进程做完第一次扫描（上面几个脚本自己会调用它） | `bridge\windows\wait_bridge_ready.bat` |
| `nav2_demo.bat` | 规划 + 驱动的演示 | `nav2_demo.bat --goal 5,-4 --report run.json` |
| `nav2_stack.bat` | 一条命令拉起整条 nav2 链，可选直接开到目标点；`--map FILE` 时改成读一张存好的图 | `nav2_stack.bat --goal auto --report g.json --timeout 120`；`nav2_stack.bat --map map\ai_map001.yaml --goal 1.54,4.23` |
| `nav2_safety.bat` | 速度平滑 + 碰撞监控（旁路跑，或配 `--safety` 进链） | `nav2_safety.bat --duration 60` |
| `nav2_docking_route.bat` | docking / route 两个节点，它们不在规划驱动链上 | `nav2_docking_route.bat --duration 60` |
| `slam_demo.bat` | slam_toolbox：`/scan` 进、`/map` 出 | `slam_demo.bat --duration 60` |
| `amcl_demo.bat` | 用 AMCL 定位（`/odom` 起一个初始位姿） | `amcl_demo.bat --initial-pose 1.5,-2,0` |

每个 bat 头部的 `rem` 注释段就是它自己的说明：全部参数、前置条件（要守护进程在跑）、
以及为什么这么设计。想一次看全，读 `bridge/docs/02-running.md`。

其中五个有**跨平台孪生**，在 `bridge\scripts\` 下、Windows 与 Ubuntu 跑同一份，参数一一对应：

| bat | 跨平台 |
| --- | --- |
| `nav2_stack.bat` | `run_nav2_stack.py` |
| `slam_demo.bat` | `run_slam_demo.py` |
| `amcl_demo.bat` | `run_amcl_demo.py` |
| `nav2_safety.bat` | `run_nav2_safety.py` |
| `nav2_docking_route.bat` | `run_nav2_docking_route.py` |

### 仓库脚本一览

| 脚本 | 作用 |
| --- | --- |
| `bridge\scripts\build_bridge.py` | 构建 Windows 桥。`--test` 顺带跑 ctest，`--clean` 先删构建目录，`--target X` 只构一个目标 |
| `bridge\scripts\sync_to_ubuntu.py` | 把 Ubuntu 侧代码同步过去；`--run` 顺带起仿真（仿真在 Ubuntu 的 tmux 会话 `sim` 里） |
| `bridge\scripts\run_nav2_demo.py` | 不经 bat，直接从 Windows 驱动 nav2 演示 |
| `bridge\scripts\acceptance_check.py` | 四条验收标准（9 条检查）一次跑完 |
| `bridge\scripts\ubuntu_ssh.py` | 跨机执行：`ubuntu_ssh.py [--file x.sh] [--timeout N] "<cmd>"` |
| `bridge\scripts\ubuntu_sim_ctl.sh` | Ubuntu 侧的仿真启停（在 Ubuntu 上跑，不是 Windows） |
| `bridge\linux\*.sh` | Linux 侧的四个入口（`bridge_env.sh` / `ros2.sh` / `ros2_bridge.sh` / `wait_bridge_ready.sh`），与 `bridge\windows\*.bat` 一对一 |
| `bridge\scripts\fsedit.py` | 精确文本替换小工具（这个仓库改文件的常规手段，见「文档纪律」） |
| `nav2\ros2_shim\build.py` | 构建兼容层 + 整个 nav2；`--no-configure` 只构建，`--jobs N` 改并行度 |
| `nav2\ros2_shim\run_tests.ps1` | 测试入口的 PowerShell 外壳：转给 `tools\run_ctest.py`（先 `shim` 再 `shm`），其余参数原样给 ctest |
| `gRPC\ros2_gRPC_sdk\scripts\build.py` | 构建通信 SDK；`--test` 顺带 ctest，`--list` 看检测到的平台 |
| `tools\run_ctest.py` / `tools\ctest_stress.py` | 跑某棵树的测试 / 并行压测（见 `tools\README.md`） |

## 怎么开发

### 铁律一：编译只用 6 个大核心

本机是 Intel Core Ultra 5 245K（Arrow Lake，无超线程）：14 个逻辑核 = 6 个 P-core + 8 个
E-core。上面所有构建入口默认 `-j 6`，并且把进程**钉在** `{0,1,10,11,12,13}` 上 —— 子进程
继承亲和性掩码，所以 cmake / ninja / g++ 全留在大核。自己写脚本时套用同一套：

```powershell
python tools\_cores.py                 # 本机判定结果： [0, 1, 10, 11, 12, 13] / mask 0x3C03
$env:BIG6_CORES = "0,1,10,11,12,13"    # 覆盖检测（按需）
$env:BIG6_NO_PIN = "1"                  # 只检测不绑定（按需）
```

判定不是照抄文档：`GetLogicalProcessorInformationEx` 的 `EfficiencyClass` 与实测一致
（400M 次依赖自增，大核 0.0811 s / 小核 0.0876 s，8% 的差距正是 5.2 GHz vs 4.6 GHz），
规则是「取成员数较少的那一类」，本机就是 6 个 P-core。检测与绑定只有一份实现：
`tools/_cores.py`。

### 铁律二：五棵测试树全绿

| 树 | 怎么跑 | 用例数 |
| --- | --- | --- |
| `nav2\ros2_shim` | `python nav2\ros2_shim\build.py` 然后 `python tools\run_ctest.py shim` | 32 |
| `nav2\shm_transport` | `python tools\run_ctest.py shm` | 1 |
| `bridge\windows` | `python bridge\scripts\build_bridge.py --test`（或 `python tools\run_ctest.py bridge`） | 6 |
| `gRPC\ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py --test`（或 `python tools\run_ctest.py grpc`） | 4 |
| `bridge\ubuntu\ros2_bridge_sim\tests` | `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q` | 71 |

* pytest 这棵**在 Windows 上跑**：Ubuntu 的 venv 里只有 grpc / numpy / protobuf，没有 pytest。
* `bridge\windows` 那一棵的 6 条里 5 条是桥自己的（`bridge_cdr_roundtrip` / `service` /
  `action` / `cli_values` / `shm_roundtrip`），第 6 条是 `add_subdirectory(nav2/shm_transport)`
  带进来的 `shm_transport` —— 和上面 `nav2\shm_transport` 那一行是同一个用例。
* 只有并行才现形的问题用 `python tools\ctest_stress.py --rounds 50 --jobs 8` 压，结果落在
  `.scratch/`（`ctest_stress.json` + 坏轮的 `stress_fail_NNN.log`）。
* `shim` 树里的用例各自带一个 `NAV2_SHM_NAMESPACE=ctest_<用例名>` —— `CMakeLists.txt` 末尾
  用 `ENVIRONMENT_MODIFICATION` 统一注入的 —— 所以守护进程开着也不会和镜像进来的机器人
  抢同一条共享内存通道。守护进程停掉同样 32/32。
* 全链路验收：`python bridge\scripts\acceptance_check.py`（9/9）。

### Linux 侧：`shm_transport` 两边都能编能测

`nav2\shm_transport` 的算法只有一份，平台差异收在 `ShmChannel::Impl` 里：Windows 用命名映射 +
命名互斥体 + 手工事件，Linux 用 `shm_open` / `mmap` + 段内的 `pthread_mutex_t`
（`PTHREAD_PROCESS_SHARED` + `ROBUST`）+ 条件变量广播，段的生命周期靠段本身的 `flock` 共享锁
（内核在进程死掉时自己收回，等价于 Windows 的「最后一个句柄」）。这条通道在两个平台上都编过、跑过：

```bash
# 一个入口，两边都能跑（Windows 上走 tools/_toolchain.py 找到的 MSYS2 UCRT64）
python nav2/shm_transport/build.py                         # configure + build + ctest
python nav2/shm_transport/build.py --build-dir nav2/shm_transport/build-dbg
python nav2/shm_transport/build.py --no-test --jobs 6
```

实测（`test_shm`：超时 / 慢读者 / 广播 / 吞吐 / 跨进程 / 写者被杀的段 / 写者还活着的段）：

| 平台 | 结果 |
| --- | --- |
| Windows / MSYS2 UCRT64（MinGW） | 5 条全绿，连跑 3 次；`python tools\run_ctest.py shm` 1/1 |
| Ubuntu 24.04 / glibc 2.39 / g++ 13.3 | 7 条全绿，连跑 5 次（含一次在 6 核全量编译负载下） |
| Alpine 3.24 / musl（Ubuntu 上的 chroot） | 7 条全绿，连跑 5 次，吞吐 1.96 M msg/s |

* 那个 Alpine 根是 `apk add build-base cmake ninja python3` 起的最小根，它的 `/dev/shm` 是
  **普通 ext4 目录、不是 tmpfs**，所以「`MAP_SHARED` 落在 tmpfs 之外的文件系统上」顺带也验了；
  `PTHREAD_MUTEX_ROBUST` 在 musl 上可用。
* Alpine 里只有**直接 `g++` 编译**跑得通：CMake + ninja 在 chroot 里会因为缺 `/proc` 报
  `posix_spawn /usr/bin/c++` 失败。别把这段读成「CMake 在 Alpine 上能跑」。
* 这一阶段在 Linux 上撞出并修掉了三个真缺陷（`abs` / `max` 缺 `<math.h>`、`nav2_amcl` 的
  `HAVE_DRAND48`、POSIX 后端的一个死锁），都在兼容层 / 传输层里，上游源码一行没动：
  `bridge/docs/04-gaps.md` 第十三节。

细节（两边各自用什么同步对象、C ABI 为什么加了 `shm_channel_` 前缀）在 `nav2/README.md`。

### 跨平台构建入口：`tools/_toolchain.py`

以前「挡着 Linux 的是构建入口」——每个构建脚本各写一份 MSYS2 路径、`.bat` / `.ps1`、`.exe` 名字。
现在工具链解析收在 `tools/_toolchain.py`（`find_tools()` / `build_env()`），核数收在
`tools/_cores.py`（`big_cores()` / `pin()`），四棵树的构建脚本都走这两个模块：

| 想改什么 | 怎么改 |
| --- | --- |
| MSYS2 装在哪 | `CPU_MSYS2_ROOT=D:\...\MSYS2`，或命令行 `--msys2` |
| 用哪个 cmake / ninja / ctest | `CMAKE=` / `NINJA=` / `CXX=` |
| 只用哪几个核 | `BIG6_CORES=0,1,10,11,12,13`；`BIG6_NO_PIN=1` 关掉亲和性 |

* Windows 上找不到 MSYS2 会报一条照着做就能改的消息；POSIX 上 `bindir` 是 `None`（没有 MSYS2 的
  `bin` 要塞进 `PATH`），编译器按 `c++` → `g++` → `clang++` 找。
* 只有 `g++`、没有 `c++` 的系统（Alpine）上，脚本会把找到的编译器显式写进
  `-DCMAKE_CXX_COMPILER`；不这么做 CMake 的 try-compile 会
  `posix_spawn /usr/bin/c++: No such file or directory`。
* `nav2/third_party/dl/btcpp_master/.../libbehaviortree_cpp.a` 原来是**没人记录的手工前置条件**
  （Linux 上表现为 `ninja: ... missing and no known rule to make it`），现在
  `python nav2\ros2_shim\build.py` 自己会先把它 configure + build 出来（`--no-btcpp` 可跳过）。
* 「本地 + 远端跑同一张表」的门禁：`python tools\check_cross_platform.py --quick`（`--local` /
  `--remote` / `--push-only` / `--key` / `--target` / `--timeout`），两边跑同一串命令并打 PASS/FAIL。
* CI：`.github/workflows/cross-platform.yml`，`windows` job（setup-msys2 的 UCRT64，
  `CPU_MSYS2_ROOT=C:/msys64`）+ `linux` job（apt 装 cmake / ninja / zlib / eigen / boost / grpc /
  protobuf / yaml-cpp），两边跑同一串入口命令（四棵树各自的构建入口 + ctest）。
  **这份 workflow 还没在任何 runner 上跑过**（仓库没有 git 远端可推），但它里面每一步
  都在下面那张表里手工跑过。
* `gRPC\ros2_gRPC_sdk\scripts\build.py` 上一轮还带着自己那份 `DEFAULT_MSYS_ROOT` /
  `big_cores()`，现在也接上了 `tools/_cores.py` / `tools/_toolchain.py`，复制品没了。

### `bridge\windows` 在 Linux 上：从「编一次」到 4/4

`bridge\windows` 这份代码在 Linux 上编过、跑过了，是这一阶段的主线：

| 平台 | configure | 构建 | ctest |
| --- | --- | --- | --- |
| Windows / MSYS2 UCRT64 | 走 `ProtobufConfig.cmake`（`Protobuf_DIR` = MSYS2） | 0 error | **4/4** |
| Ubuntu 24.04 / g++ 13.3 | 走 CMake 自带的 `FindProtobuf` 模块（系统 protobuf 3.21.12） | 0 error | **4/4** |

挡住它的两件事都不是平台分支写错，而是「只在 Windows 上成立」的假设：

* `find_package(Protobuf CONFIG REQUIRED)`——MSYS2 带 `ProtobufConfig.cmake`，Ubuntu 的
  `libprotobuf-dev` **一个 CMake config 都不带**（同目录的 `gRPCConfig.cmake` 自己也在
  `find_package(Protobuf)`）。现在先 CONFIG、没有再 MODULE，Windows 那条路一个字没变。
* `ros2_bridge_client.cpp` 只 `#include <grpc/impl/channel_arg_names.h>`——这个头是 gRPC 1.55
  才有的，Ubuntu 的 `libgrpc++-dev` 还是 1.51.1（这时 `GRPC_ARG_*` 在
  `grpc/impl/codegen/grpc_types.h`）。改成 `__has_include` 守卫。

顺带补掉了全仓库唯一一处真桩：`bridge_daemon.cpp` 的
`#else void detach_process(...) {}`。POSIX 侧的 `freopen` + `setvbuf` 和 Windows 侧本来就是同一对
调用，只有 `FreeConsole()` 与 `NUL` / `/dev/null` 不同，所以合成一个函数而不是留空壳 ——
空壳会让 `--detach` 在 Linux 上惄惄变成「保留调用者的 stdout」，正是 Windows 那份代码要
防的事。同一个 `find_package(Protobuf)` 问题在 `gRPC\ros2_gRPC_sdk\CMakeLists.txt` 也有一份，一起
修了：**通信 SDK 本身也第一次在 Linux 上 configure / build / ctest 全过（4/4）**。

`bridge\linux` 现在有了对应 `ros2_bridge.bat` 的四个入口，而且在 Ubuntu 上真跑通了：守护进程起来、
`ros2 topic list -t` 打出与 Windows 一样的七条话题带类型、本机 `topic pub /cmd_vel` 走上行
（守护进程计数 19 条）、`stop` 之后 `pid` 返回 1。

还没做的：上面这份 workflow 仍然没在 runner 上跑过；CI 里 linux job 的 apt 那串包是照着
这台机器 `dpkg -l` 抄的、没在干净容器里验过。
详细证据链在 `bridge/docs/04-gaps.md` 第十三、十四节。

### 改哪里

| 想改的东西 | 落点 |
| --- | --- |
| Windows 上 `ros2` 命令的行为 | `bridge\windows\tools\ros2_cli.cpp` |
| 守护进程：发现 / 重连 / 镜像 / 图维护 | `bridge\windows\src\bridge_daemon.cpp` |
| CDR ↔ 共享内存转码、跨机类型表 | `bridge\windows\src\type_registry.cpp` + `include\ros2_bridge\cdr_archive.hpp` |
| 跨机 service / action 转发 | `bridge\windows\src\service_registry.cpp` / `action_registry.cpp` |
| gRPC 协议（加 RPC、改字段） | `gRPC\ros2_gRPC_sdk\proto\ros2_bridge.proto`，然后 `scripts\build.py` 重新生成桩 |
| Ubuntu 侧仿真（世界、雷达、小车） | `bridge\ubuntu\ros2_bridge_sim\world.py` / `sim.py` |
| Ubuntu 侧话题 / 服务 / 动作表 | `bridge\ubuntu\ros2_bridge_sim\messages.py` / `services.py` / `graph.py` |
| 兼容层接口（rclcpp / rcl / 图注册表 / 参数 / tf） | `nav2\ros2_shim\include\ros2_shim\` + `src\` |
| 消息结构体的生成规则 | `nav2\ros2_shim\tools\generate_messages.py`，改完用 `generate.ps1` 重生成 |
| 某个 nav2 服务器的 main / 参数装载 | `nav2\ros2_shim\apps\<包>_node.cpp` |
| nav2 本身的行为 | `nav2\nav2_extracted\navigation2-1.5.1\<包>\`（兼容层直接从这里编译） |

关于最后两行，值得写清楚：`nav2\ros2_shim\CMakeLists.txt:96` 起把上游源码**原地**编进来 ——
`nav2_package_sources()` 会 glob 每个包的 `src/**.cpp` 与 `plugins/**.cpp`，并且自动丢掉
自带 `main()` 的文件（那些位置交给 `apps/`）；每个编译单元都被 `-include prelude.hpp`
强制注入一层垫片；个别上游头文件由 `generated_nav2/` 遮蔽。所以：

* 能用兼容层解决的（缺的接口、类型、平台差异）就**别动上游源码**，加在
  `prelude.hpp` / `include\ros2_shim\` 里 —— 对升级最友好；
* 真必须改上游的，改动要能被 `nav2\patches\` 里的 patch + `verify_patch.py` 表达出来并复现，
  否则下次换上游版本会悄悄丢掉；
* 要排除整个文件，用 `nav2_port_package(<target> <包> <排除的文件...>)`，而不是把文件 stub 掉。

### 走一遍：加一个跨机消息类型

以「Ubuntu 发得出来、Windows 上也认这个类型」为目标，一共六步：

1. **生成结构体**：`nav2\ros2_shim\generate.ps1`（或按它里面的参数手动跑
   `tools\generate_messages.py --root ... --output generated\include --python-output python`）。
   扫描顺序有意义：先 overlay 后上游，先命中者胜。生成物进 `generated/`，不要手改。
2. **教桥上怎么转码**：`bridge\windows\src\type_registry.cpp` 里有三处要加 —— 编码分支、
   解码分支（两个 `if constexpr` 链），以及底部的表项
   `{"pkg/msg/Type", makeCodec<pkg::msg::Type>("pkg/msg/Type")}`。
3. **造基准字节**：`python bridge\tests\generate_fixtures.py` —— 用 `rosbags`（真 ROS2 的
   CDR 实现）生成 `<名字>.cdr` 与对应的 `.shim`，外加 `manifest.tsv`。
4. **逐字节自检**：`bridge\windows\build\bridge_cdr_check.exe`（也就是 `bridge` 树里的
   `cdr_check` 用例）会拿 fixture 比对自己的转码结果。
5. **让仿真真的发得出来**：把类型加到 `bridge\ubuntu\ros2_bridge_sim\messages.py`。
6. **两棵树都跑一遍**：`python tools\run_ctest.py bridge` 与
   `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q`。

### 走一遍：加一条 ctest

1. 把测试写进 `nav2\ros2_shim\tests\xxx.cpp`（用 `ros2_shim` 提供的接口，断言风格照抄邻近文件）。
2. 在 `nav2\ros2_shim\CMakeLists.txt` 里 `add_executable` + `target_link_libraries(... ros2_shim)`
   + 一行 `add_test(NAME ros2_shim_xxx COMMAND test_xxx)`。
3. 什么都不用做，`NAV2_SHM_NAMESPACE` 会在文件末尾被自动注入；要额外环境变量就写
   `set_tests_properties(... PROPERTIES ENVIRONMENT "NAV2_SHM_LOG_LEVEL=info")`，
   和注入的命名空间不冲突（`ENVIRONMENT_MODIFICATION` 是追加语义）。
4. `python nav2\ros2_shim\build.py && python tools\run_ctest.py shim -R xxx`。
5. 改测试断言时先确认它**会**失败：把断言改坏、看它红，再改回来 —— 这条在
   `bridge/docs/03-validation.md` 里叫「变异验证」，是这个仓库的常规要求。

### 文档纪律

这个仓库的文档是**脚本改出来的**，不是编辑器敲出来的，所以有几个固定的坑：

* `.md` 一律 **LF**；写中文的用 **UTF-8 BOM**（否则 Windows 工具按 GBK 猜，整篇变乱码）；
  `.bat` 反过来，必须是 **CRLF**。
* 改文件走 `bridge\scripts\fsedit.py`（`write` / `replace` 两个子命令），替换前先断言
  目标文本只出现一次 —— PowerShell 下 `apply_patch` 会把换行弄坏。
* 收尾体检：`python tools\check_docs.py --quiet`。它抓 CR、缺 BOM、``` 围栏不配对、
  控制字符。错误返回码为 1；缺 BOM 只算警告（`bridge\`、`gRPC\ros2_gRPC_sdk\` 两份老 README 就是没有 BOM 的）。
* 动了 `.scratch/` 之前先跑 `python tools\find_scratch_refs.py --check`：那里的文件是
  `bridge/docs` 的证据，路径断了「证据 N」就成了死链。

### 收尾清单

1. `python tools\check_docs.py --quiet` 干净。
2. `python tools\check_structure.py` 干净；加/删过目录就同步根 `README.md` 的「目录结构」和
   那棵树的 README，这条会替你验。
3. 动过的树各自构建 + 测试通过（五棵树见上）。
4. 动了跨机行为 → `python bridge\scripts\acceptance_check.py` 四条验收重跑一遍。
5. 动了 `.scratch/` → `python tools\find_scratch_refs.py --check` 为 0。
6. 把新证据（日志、地图、报告）放进 `.scratch/`，把能复用的工具放进 `tools/`。

## 文档

### 在线文档站（推荐，中英双语，GitHub Pages）

**https://xiaoyuyu520520.github.io/mini-ros2/**

| 章节 | 页面 |
| --- | --- |
| **1 快速开始** | [概述](https://xiaoyuyu520520.github.io/mini-ros2/Quick_start/overview) · [Windows 上手](https://xiaoyuyu520520.github.io/mini-ros2/Quick_start/windows) · [Ubuntu 上手](https://xiaoyuyu520520.github.io/mini-ros2/Quick_start/linux) · [连接机器人](https://xiaoyuyu520520.github.io/mini-ros2/Quick_start/connect) |
| **2 系统配置** | [环境变量配置](https://xiaoyuyu520520.github.io/mini-ros2/System_configuration/environment) · [网络与端口配置](https://xiaoyuyu520520.github.io/mini-ros2/System_configuration/network) · [共享内存几何配置](https://xiaoyuyu520520.github.io/mini-ros2/System_configuration/shm) |
| **3 基础应用开发** | [ros2 命令行](https://xiaoyuyu520520.github.io/mini-ros2/Basic_application/ros2_cli) · [运行 nav2 导航栈](https://xiaoyuyu520520.github.io/mini-ros2/Basic_application/nav2_stack) · [SLAM 建图](https://xiaoyuyu520520.github.io/mini-ros2/Basic_application/slam) · [地图存取](https://xiaoyuyu520520.github.io/mini-ros2/Basic_application/map) |
| **4 架构说明** | [总体架构](https://xiaoyuyu520520.github.io/mini-ros2/Architecture/overview) · [共享内存传输](https://xiaoyuyu520520.github.io/mini-ros2/Architecture/shm_transport) · [Action 实现](https://xiaoyuyu520520.github.io/mini-ros2/Architecture/action) |
| **5 能力清单** | [已实现能力](https://xiaoyuyu520520.github.io/mini-ros2/Capability/overview) · [验收与实测数据](https://xiaoyuyu520520.github.io/mini-ros2/Capability/validation) |
| **6 常见问题** | [功能限制与缺口](https://xiaoyuyu520520.github.io/mini-ros2/FAQ/limitations) · [故障排查](https://xiaoyuyu520520.github.io/mini-ros2/FAQ/troubleshooting) |
| **7 进阶开发** | [开发指南](https://xiaoyuyu520520.github.io/mini-ros2/Advanced/development) |

> 文档站的源文件在 [`docs/`](docs/) 下（Jekyll），推送 `main` 后 GitHub Pages 自动构建发布。
> 改文档就改 `docs/**/*.md`，不要只改这里。

### 仓库内文档

| 文档 | 内容 |
| --- | --- |
| [`BUILD.md`](BUILD.md) | 编译 / 启动 / 停止速查：Windows 与 Linux 各一块，逐项目写清进哪个目录、敲什么命令、怎么算编过了、怎么加载环境、怎么停 |
| [`bridge/README.md`](bridge/README.md) | 桥接层总览、目录、常用命令 |
| [`bridge/docs/01-architecture.md`](bridge/docs/01-architecture.md) | 组件、上下行消息生命周期、转码正确性、已知限制 |
| [`bridge/docs/02-running.md`](bridge/docs/02-running.md) | 运行手册：构建、bat 入口、全部参数、环境变量、排错 |
| [`bridge/docs/03-validation.md`](bridge/docs/03-validation.md) | 验收标准、实测证据、复现环境、未覆盖项 |
| [`bridge/docs/04-gaps.md`](bridge/docs/04-gaps.md) | 还差什么：nav2 / slam_toolbox 的缺口、各自的代价、补的顺序 |
| [`bridge/docs/05-audit.md`](bridge/docs/05-audit.md) | 审查（2026-09-17）：独立复核能力与证据、补上的两条测试、五棵树的隔离性与并行重复性、缺口逐条重量 |
| [`bridge/ubuntu/ros2_bridge_sim/README.md`](bridge/ubuntu/ros2_bridge_sim/README.md) | 仿真世界 / 雷达 / 小车 / gRPC agent 的设计 |
| [`nav2/README.md`](nav2/README.md) | 兼容层 / 共享内存传输 / slam_toolbox 移植的总览与开发规矩 |
| [`nav2/ros2_shim/README.md`](nav2/ros2_shim/README.md) | 兼容层与 nav2 移植：产物边界、构建与测试、三条改法规矩 |
| [`tools/README.md`](tools/README.md) | 仓库级工具：跑测试、并行压测，以及文档 / 结构 / `.scratch` / 跨平台四项体检 |

上面这张表只列「值得整体读一遍」的那几份；每个子目录自己的 README 在它那一层（见前面
「一棵树一张 README」）。

## 本机环境备忘

| 项 | 位置 |
| --- | --- |
| Python 虚拟环境 | `E:\venvs\py312`（`& "E:\venvs\py312\Scripts\Activate.ps1"`） |
| 临时用清华源装包 | `pip install -i https://mirrors.tuna.tsinghua.edu.cn/pypi/web/simple 包名` |
| MSYS2 UCRT64 | `D:\WindowsServer2025_1\software\MSYS2\ucrt64.exe`（工具链在 `ucrt64\bin`） |
| tmux | `D:\WindowsServer2025_1\software\psmux-v3.3.6-windows-x64\tmux.exe` |
| Ubuntu 侧工作目录 | `~/code/CPU`，虚拟环境 `~/code/CPU/.venv` |

Windows 侧的一切都是**临时的**：`bridge_env.bat` 只设置当前 cmd 窗口的环境变量（PATH、
共享内存几何、日志级别、SDK 配置位置），关掉窗口或 `endlocal` 就恢复原样，不写注册表、
不改系统 PATH。
