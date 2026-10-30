# bridge — 让 Windows 用上 Ubuntu 的 ROS2 话题

`bridge` 把两台机器拼成一个 ROS2 系统：

* **Ubuntu** 跑一个 Python 仿真的差速小车（雷达 / 里程计 / 地图 / TF），扮演真实机器人；
* **gRPC 通信 SDK**（`gRPC/ros2_gRPC_sdk`）把话题跨机传输；
* **共享内存 ROS2 兼容层**（`nav2/ros2_shim`）把收到的消息变成 Windows 本地的 ROS2 话题，
  让魔改版 nav2 可以像在 Linux 上一样订阅、规划、发布。

```
Ubuntu（仿真小车）                                 Windows
-------------------                                -------
/scan /odom /map /tf /clock  --gRPC-->  ros2_bridge_daemon.exe  --共享内存-->
                                                             |
                                            +---------------------------------+
                                            |  nav2 / ros2 CLI / 你的节点     |
                                            |  （共享内存兼容层 + 魔改 nav2） |
                                            +---------------------------------+
                                                             |
小车开始移动  <--gRPC--  ros2_bridge_daemon.exe  <--共享内存--  /cmd_vel
```

## 目录结构

| 路径 | 作用 |
| --- | --- |
| `windows/bridge_env.bat` | **唯一**的环境入口：临时设置 PATH / 共享内存几何 / 日志级别，不改系统 |
| `windows/ros2.bat` | 在 Windows 上使用 ROS2 的统一入口（自动确保守护进程在跑） |
| `windows/ros2.ps1` | 同一件事的 PowerShell 版本：点源一次，之后本窗口直接敲 `ros2`（`.bat` 改不了 PowerShell 会话的环境，所以单独有一份） |
| `windows/ros2_shell.bat` | 开一个已经点源好 `ros2.ps1` 的新 PowerShell 窗口（`.bat` 拿不到调用方会话的环境，只能开新窗口） |
| `windows/ros2_bridge.bat` | 守护进程控制：`start` / `run` / `status` / `stop` |
| `windows/nav2_demo.bat` | 运行 nav2 规划 + 控制 demo |
| `windows/nav2_stack.bat` | 上游 nav2 本体一键起停（AMCL + planner / controller / bt_navigator / behavior_server / smoother_server / waypoint_follower + lifecycle_manager）。**起栈前先 `/sim/reset`**：AMCL 拿 `/odom` 当初始位姿，车停在别处会让每一次规划都报 `Start Coordinates ... outside bounds`。加 `--safety` 时速度平滑 + 碰撞监控**进链**（见下） |
| `windows/nav2_safety.bat` | 旁路安全链：`velocity_smoother` + `collision_monitor`（`/cmd_vel` → `/cmd_vel_smoothed` → `/cmd_vel_monitored`），**只观察、不动车**；进链用 `nav2_stack.bat --safety`。跨平台孪生 `scripts/run_nav2_safety.py` |
| `windows/nav2_docking_route.bat` | 旁路起 `docking_server` + `route_server` 并推到 ACTIVE（没有 dock 实例 / 图文件，只是「装得起来」）。跨平台孪生 `scripts/run_nav2_docking_route.py` |
| `windows/slam_demo.bat` | 用 `slam_toolbox` 建图：`/scan` 进、`/map` 出 |
| `windows/amcl_demo.bat` | 用 `nav2_amcl` 定位（`/odom` 给初始位姿，用共享内存里的 `/scan` `/map` `/tf`）。跨平台孪生 `scripts/run_amcl_demo.py` |
| `windows/wait_bridge_ready.bat` | 等守护进程做完第一次扫描（其余脚本自己会调用它） |
| `windows/src/bridge_daemon.cpp` | 守护进程：发现、持久连接、镜像下行、转发上行 |
| `windows/src/type_registry.cpp` | CDR ↔ 共享内存结构体 的逐字节转码表（40 种类型，2026-09-17 深夜加 `Image` / `CameraInfo`，证据 62） |
| `windows/src/service_registry.cpp` | service 的 CDR ↔ 共享内存转码表（38 种类型：`slam_toolbox` 的全部 11 个 srv、**`nav2_msgs` 的全部 20 个 srv**、`nav_msgs/srv/GetMap`，加上 `lifecycle_msgs` 的 3 个与 `std_srvs` 的 3 个） |
| `windows/src/action_registry.cpp` | 跨机 action 的 codec 表（20 个：仿真自己的 `Drive` + `nav2_msgs` 的**全部 19 个 `.action`**，每一条都带 Goal / Result 的逐字段打印器，见证据 61） |
| `windows/include/ros2_bridge/sim_action_messages.hpp` | 仿真 `Drive` action 的 Goal/Feedback/Result 兼容层结构体 |
| `windows/tests/action_roundtrip.cpp` | action 链路的往返测试（goal/拒绝/取消/反馈/结果/未知 target + 20 个类型，373 项断言） |
| `windows/tools/ros2_cli.cpp` | `ros2 topic` / `service` / `action` / `param` / `node` / `bridge status` 等命令 |
| `../nav2/ros2_shim/config/bt/` | 行为树 XML（保留上游末尾的恢复轮盘 `Spin` / `Wait` / `BackUp`，由 `behavior_server` 服务） |
| `ubuntu/ros2_bridge_sim/` | Ubuntu 侧：世界模型、雷达、小车、gRPC agent（纯 Python） |
| `scripts/` | 构建、跨机同步、运行 demo、验收自检、SSH 小工具 |
| `tests/` | CDR 转码的一致性用例（含 fixture 生成器） |
| `docs/` | 架构、运行手册、验收记录、缺口清单、审查（`05-audit.md`） |

仓库根目录还有 `tools/`（跑测试、压测、文档体检、证据引用扫描）与 `.scratch/`（历次迭代的实测证据），
以及一份把整个仓库从零讲到开发的顶层 `README.md`。

## 快速开始

```bat
:: 1) 构建 Windows 侧（守护进程 + ros2 CLI）
python bridge\scripts\build_bridge.py

:: 2) 在 Ubuntu 上启动仿真小车 + gRPC agent
::    python bridge\scripts\ubuntu_ssh.py --file bridge\scripts\ubuntu_sim_ctl.sh -- start

:: 3) 在 Windows 上使用 ROS2（首次调用会自动拉起守护进程、扫描、建立持久连接）
bridge\windows\ros2.bat topic list -t

:: 4) 让 nav2 建图 + 规划 + 指挥小车移动（自己接的闭环，用 nav2 的库）
python bridge\scripts\run_nav2_demo.py --goal auto

:: 4b) 或者用上游 nav2 的服务器本体：planner + controller + bt_navigator + lifecycle_manager
::     跨平台入口（Windows 与 Ubuntu 同一份；.bat 是同一件事的 cmd 外壳）
python bridge\scripts\run_nav2_stack.py --goal auto --report goal.json
python bridge\scripts\run_nav2_stack.py stop
::     建图同理：python bridge\scripts\run_slam_demo.py --duration 60

:: 4c) 地图的存取：把 /map 存成 PGM 或 8 位 PNG + YAML，之后再由文件发回 /map（map_server.exe）
call bridge\windows\bridge_env.bat
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt pgm
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt png  :: scale 模式要 png 才带透明通道

:: 5) 四条验收标准（9 条检查）一次跑完（冷启动扫描、上行控制、nav2 移动、SLAM 建图）
python bridge\scripts\acceptance_check.py
```

Linux 侧是四个对应的 `.sh`（`bridge/linux/`）：`source bridge/linux/bridge_env.sh` 之后直接敲
`ros2 topic list -t`，或者 `bash bridge/linux/ros2.sh topic list -t` 一条命令了事；守护进程用
`bash bridge/linux/ros2_bridge.sh start|status|stop`。守护进程和 agent 在同一台机器上时要给
`ROS2_BRIDGE_AGENT=127.0.0.1:50051` —— 发现刻意不看本机（`04-gaps.md` 第十四节）。

nav2 的服务器栈与建图不必在 Linux 上再写一遍：`python3 bridge/scripts/run_nav2_stack.py --goal auto`、
`python3 bridge/scripts/run_slam_demo.py --duration 60` 与 Windows 是同一份脚本，逐步对照见 `BUILD.md`。

`ros2.bat` 输出的就是标准 ROS2 的话题视图：

```
/clock [rosgraph_msgs/msg/Clock]
/cmd_vel [geometry_msgs/msg/Twist]
/map [nav_msgs/msg/OccupancyGrid]
/odom [nav_msgs/msg/Odometry]
/scan [sensor_msgs/msg/LaserScan]
/tf [tf2_msgs/msg/TFMessage]
/tf_static [tf2_msgs/msg/TFMessage]
```

PowerShell 里用同一个入口要**点源** `windows\ros2.ps1`（`.bat` 影响不了 PowerShell 会话，直接跑
`ros2.bat` 只会打印用法）：

```powershell
. E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1
ros2 topic list -t
```

下面「常用命令」一节都写成 `ros2.bat ...`；已经点源过的 PowerShell 窗口把前缀去掉即可
（`ros2 topic list -t`）。新窗口不用管，profile 已经替你做掉了。

## 常用命令

```bat
ros2.bat topic list -t                     :: 话题 + 类型
ros2.bat                                    :: 不带参数就打印全部子命令
ros2.bat topic info /scan                  :: 发布者/订阅者/来源（remote 还是 local）
ros2.bat topic echo /odom --count 3        :: 打印消息内容
ros2.bat topic hz /scan                    :: 实时频率（Ctrl+C 结束）
ros2.bat topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.3 angular.z=0.1 --once
ros2.bat topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.3, z: 0.1}}" -1   :: 真 ROS2 那份 YAML 写法
ros2.bat topic pub /scan sensor_msgs/msg/LaserScan "{ranges: [1.0, 2.0, 3.0]}" -1 -p :: -p 先把要发的消息打出来
ros2.bat service list -t                    :: 服务 + 类型
ros2.bat service call /sim/status std_srvs/srv/Trigger            :: 调一个服务
ros2.bat service call /sim/set_paused std_srvs/srv/SetBool "{data: true}"   :: 请求同样是 YAML
ros2.bat param list /controller_server -t   :: 参数 + 类型（调的是节点自己名字下那六个服务，只在本机）
ros2.bat param get /controller_server controller_frequency
ros2.bat param set /controller_server min_x_velocity_threshold 0.5
ros2.bat param describe /controller_server controller_frequency
ros2.bat param dump /bt_navigator           :: 导成能直接喂回 --params-file 的 YAML
ros2.bat action list -t                     :: 动作 + 类型（本地节点 + 远端镜像的）
ros2.bat action type /sim/drive             :: 动作的类型
ros2.bat action send_goal /sim/drive ros2_bridge_sim/action/Drive distance=1.0
ros2.bat action send_goal /sim/spin nav2_msgs/action/Spin target_yaw=1.5 --timeout 25
ros2.bat action send_goal /sim/spin nav2_msgs/action/Spin "{target_yaw: 1.5}" -t 25 :: 目标也是 YAML
ros2.bat action send_goal /sim/back_up nav2_msgs/action/BackUp target.x=-0.4 speed=-0.2
ros2.bat service call /sim/set_initial_pose nav2_msgs/srv/SetInitialPose ^
        pose.header.frame_id=map pose.pose.pose.position.x=2.0 pose.pose.pose.orientation.w=1.0
ros2.bat service call /sim/enable nav2_msgs/srv/Toggle enable=true
ros2.bat action cancel /sim/drive 7         :: 取消一个还在跑的目标（id 见 send_goal 的输出）
ros2.bat node list                          :: 节点
ros2.bat bridge status                     :: 桥接状态：远端、镜像与上行统计
ros2.bat service call /sim/reset std_srvs/srv/Trigger   :: 起栈前先把小车放回原点（见下）
nav2_stack.bat --goal auto                 :: 真·nav2 栈（含 AMCL），并自动开一段
nav2_stack.bat --safety --goal auto        :: 同上，但速度平滑 + 碰撞监控进链：控制器/行为服务器
                                           :: 改发 /cmd_vel_nav，collision_monitor 成为 /cmd_vel 的唯一写者
                                           :: （--safety-params FILE 再叠一份参数，用来改限速和停止多边形）
nav2_stack.bat stop                        :: 停掉 nav2_stack 起的七个进程
ros2.bat topic pub /controller_selector std_msgs/msg/String data=MPPIFollowPath --once
                                           :: 运行期换控制器（FollowPath=RPP / DWBFollowPath / MPPIFollowPath /
                                           :: GracefulFollowPath；
                                           :: rotation_shim 占的就是 FollowPath 这个 id，见 03-validation 证据 25）
nav2_safety.bat                            :: 旁路起 velocity_smoother + collision_monitor（只观察）
nav2_safety.bat stop                       :: 停掉这两个
nav2_docking_route.bat --duration 60       :: 旁路起 docking_server + route_server，各推到 ACTIVE
nav2_docking_route.bat stop                :: 停掉这两个
:: 以上四个（含 amcl_demo / slam_demo）在 Linux 上换成 bridge/scripts/ 里的同名 run_*.py
nav2\ros2_shim\build\nav2_waypoints.exe --goal auto --count 3   :: 规划 → 平滑 → 巡航三个 action 串跑
map_saver_cli -t /map -f C:\maps\office --fmt pgm   :: 把 /map 存成 PGM（或 --fmt png）
                                           :: + YAML，不依赖 GraphicsMagick
map_server --params-file nav2\ros2_shim\config\nav2_map_server.yaml ^
           -p yaml_filename:=C:\maps\office.yaml    :: 从文件把 /map 发出来
ros2.bat service call slam_toolbox/serialize_map slam_toolbox/srv/SerializePoseGraph ^
           filename=C:/maps/office                    :: 位姿图存档：office.posegraph + office.data
                                                      :: （下一条读回来，两条路都验过，见证据 41）
slam_demo.bat -p map_name:=/slam_map -p map_file_name:=C:/maps/office ^
           -p map_start_at_dock:=true                 :: 起来就把它读回来，不重头建图
ros2_bridge.bat stop                       :: 停止守护进程
```

`topic pub` / `service call` / `action send_goal` 的取值（最后一个位置参数）两种写法都收：
真 ROS2 的那份 YAML（`"{linear: {x: 0.5}}"`、`"{data: Hello World}"`、`"{ranges: [1.0, 2.0]}"`，以及块式缩进的
`"linear:` 换行 `  x: 0.5"`；引号是给 shell 的别丢），和本项目一直在用的扁平写法
（`linear.x=0.5 data=hi`、`linear={x: 0.5}`、`ranges=[1.0, 2.0]`）。两者落到同一组字段赋值上，
**装不下的值当场报错、不会静默发一条别的消息**。
短选项：`topic pub` 的 `-1`=`--once`、`-r`=`--rate`、`-t`=`--times`、`-p`=`--print`；
`action send_goal` 的 `-t`=`--timeout`。详见 `docs/02-running.md` 的「取值」一节与证据 64。

## 关键设计点

* **环境是临时的**：`bridge_env.bat` 只设置当前窗口的环境变量，关掉窗口即恢复原样。
* **守护进程自分离**：`--detach` 让守护进程把 stdout/stderr 指向日志文件并释放控制台，
  所以 `ros2.bat` 可以被管道/重定向调用而不会挂住（见 `docs/02-running.md` 的排错一节）。
  重定向本身在两个平台上是同一对 `freopen` + `setvbuf`，只有 `FreeConsole()`、和
  `NUL` / `/dev/null` 的写法不同，所以 POSIX 侧不是一个空桩（空桩会让「挂住」这个 bug
  在 Linux 上照样出现，只是换成「保留调用者的 stdout」）。
* **这份代码两边都编过**：`bridge\windows` 在 MSYS2 UCRT64 和 Ubuntu 24.04 上都是 0 error +
  `ctest` 4/4。挡住 Linux 的不是平台分支，而是 `find_package(Protobuf CONFIG REQUIRED)`（Ubuntu 的
  `libprotobuf-dev` 不带 CMake config）与一个只有 gRPC 1.55+ 才有的头；详细证据链在
  `docs/04-gaps.md` 第十三节。
* **构建入口与平台无关**：Python 入口不写死工具链路径（见 `../tools/_toolchain.py`）、
  不写死核数（见 `../tools/_cores.py`），所以同一句 `python bridge/scripts/build_bridge.py --test`
  在两边都能跑。
* **共享内存是本地总线**：通道名 `ros2shim<topic>`，几何由 `NAV2_SHM_*` 决定，
  发布/订阅双方必须一致（`bridge_env.bat` 负责统一）。一个通道只有一个写者，所以
  要让两套栈同时活着就得用 `NAV2_SHM_NAMESPACE` 给其中一套加前缀。
* **共享内存是这个里的总线，图是它的发现机制**：订阅计数是跨进程合并出来的，发布者手里
  也必须已经是解析过的绝对名（`/cmd_vel`），否则 nav2 会看到 `0 subscribers` 然后静默不发。
  动作（action）与**服务（service）**走同一套环，环在写到一半的进程退出后不会自己清空，
  所以 `Server` / `Client` 建起来都要先 `seek_to_latest()` 跳过上一轮的残留 —— action 那两处
  一直是对的，**service 那两处一直漏着**（`rclcpp::Service` / `rclcpp::Client`），
  直到证据 41 才补上：同名服务被第二个服务端再建一次时，`<name>/_request` 环里上一轮的
  请求会被立刻重放一遍。一个 action 是 6 条通道
  （`_goal` / `_goal_ack` / `_cancel` / `_cancel_ack` / `_feedback` / `_result`），
  载荷前面是 8 字节 goal id；细节见 `docs/01-architecture.md`，排查时把
  `ROS2_SHIM_ACTION_TRACE=1` 打开就能看到每条报文的收发；`NAV2_SHIM_BT_TRACE=1` 让
   `bt_navigator` 定期打印行为树里每个节点的状态（排查「目标被接受、树却不动」）。

## 文档

* [docs/01-architecture.md](docs/01-architecture.md) — 组件、消息生命周期、设计取舍
* [docs/02-running.md](docs/02-running.md) — 运行手册、参数、环境变量、故障排查
* [docs/03-validation.md](docs/03-validation.md) — 验收标准与实测证据
* [docs/04-gaps.md](docs/04-gaps.md) — 距离完整的 slam_toolbox / nav2 还差什么
