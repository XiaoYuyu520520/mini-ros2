# racecar —— 建图 / 导航工作区（SLAM + nav2）

`code/Desktop/racecar` 是从真车工作区（真机上在 `/root/Desktop/racecar`）搬进 CPU 仓库的 ROS2 源码树。
**这里只留建图（SLAM）和导航（nav2）**：底盘启动、EKF、串口、摄像头、AI 推理都已经移到
`../rubblish/`，见 `../rubblish/README.md`。

这套代码有两个后端，共用一个入口 `tools/racecar_ctl.py`：

* `shim` —— 本仓库自带的那套：nav2 与 slam_toolbox 是 `nav2/` 下面的移植二进制，话题走 gRPC 桥
  （`bridge/`），参数文件就是下面那两个 yaml。
* `ros2` —— 真车上的 ROS2 安装（`ros2 launch racecar Run_nav.launch.py ...`）。

`auto`（默认）在看得见 `bridge/scripts/run_nav2_stack.py` 时选 `shim`，否则选 `ros2`。

## 目录

```text
racecar/
├── nav.bat  nav.sh           导航    -> tools/racecar_ctl.py nav
├── gmapping.bat gmapping.sh  建图    -> tools/racecar_ctl.py slam
├── save.bat save.sh          存图    -> tools/racecar_ctl.py save-map
├── map.md                    SLAM 排查笔记（TF 树 / RViz fixed frame / 雷达话题）
├── map/                      地图；save-map 写这里，map_eraser.py 擦多余像素
├── tools/racecar_ctl.py      唯一入口（nav / slam / save-map / run / stop / status /
│                            mirror-all / where）
├── tools/check_mission_scripts.py  17 个航点脚本经兼容层逐个加载、航线逐个对齐
│                            （改完脚本先跑它，门禁 racecar-build 的本地那一半就是它）
└── src/
    ├── racecar/                       主包：配置 + launch + 航点脚本
    │   ├── config/nav.yaml            ★ nav2 的全部参数：AMCL、两个 costmap、Smac Hybrid、RPP、BT 路径
    │   ├── config/slam_toolbox.yaml   slam_toolbox 参数
    │   ├── config/help.md             goal_checker / 到点容差怎么调
    │   ├── config/laser_box/          雷达角度裁剪（建图用）
    │   ├── launch/Run_nav.launch.py   waypoint_cycle + bringup
    │   ├── launch/bringup_launch.py   nav2 bringup（planner_server / bt_navigator ...）
    │   ├── scripts/                   航点脚本（17 个 .py，要 rclpy）+ 4 条航线 CSV
    │   │                              ai_test*.csv 是赛道航线，sim_room12.csv 是仿真器那间 12 m 房间；
    │   │                              读哪一条见下面「航点脚本和它们的航线」
    │   └── rviz/  map/  maps/         RViz 配置与随包地图
    ├── nav2_waypoint_cycle/           航点循环（Run_nav.launch.py 会起）
    ├── slam_gmapping/                 gmapping 建图（ROS2 封装）—— 车上用
    ├── openslam_gmapping/             gmapping 本体，118 个文件（slam_gmapping 的上游）
    ├── lslidar_driver/ lslidar_msgs/  镭神雷达驱动与消息 —— 车上用
    └── README.md                      其实是 lslidar 的 README，放错了一层
```

## 用法

Windows / 本仓库（`shim` 后端）：

```powershell
E:\venvs\py312\Scripts\python.exe code\Desktop\racecar\tools\racecar_ctl.py status
E:\venvs\py312\Scripts\python.exe code\Desktop\racecar\tools\racecar_ctl.py slam
E:\venvs\py312\Scripts\python.exe code\Desktop\racecar\tools\racecar_ctl.py save-map -f ai_map001
E:\venvs\py312\Scripts\python.exe code\Desktop\racecar\tools\racecar_ctl.py nav --goal auto
E:\venvs\py312\Scripts\python.exe code\Desktop\racecar\tools\racecar_ctl.py stop
```

一条完整的仿真演示。`/sim/reset` 必须在**起栈之前**：`nav` 会给 AMCL 传 `--initial-pose-from-odom`，
顺序反了它拿到的是上一趟留下的 odom 位姿，`map -> odom` 从第一个 tick 就是错的。

```powershell
E:\venvs\py312\Scripts\python.exe code\Desktop\racecar\tools\racecar_ctl.py stop
cmd /c "bridge\windows\ros2.bat service call /sim/reset std_srvs/srv/Trigger"
E:\venvs\py312\Scripts\python.exe code\Desktop\racecar\tools\racecar_ctl.py nav --map live
E:\venvs\py312\Scripts\python.exe code\Desktop\racecar\tools\racecar_ctl.py run go.py sim_room12.csv
```

`run <script> [args]` 把这台机器上的 ROS2 交给 `src/racecar/scripts/` 里那个脚本：`shim` 后端走
`nav2/ros2_shim/python/shim_run.py`（shim 自己的 `rclpy` 和生成的消息类），`ros2` 后端走
`ros2 run racecar <script>`。裸名字在 `src/racecar/scripts/` 下找，脚本自己能收到后面的参数。

真车 / Ubuntu（`ros2` 后端）：

```bash
source ~/Desktop/racecar/install/setup.bash
bash ~/Desktop/racecar/nav.sh
```

每个子命令的完整选项在 `tools/racecar_ctl.py --help` 里。

## 航点脚本和它们的航线

`src/racecar/scripts/` 里 17 个脚本按入口分五类（`tools/check_mission_scripts.py` 会把 17 个逐个
加载、把它们的航线对齐一遍）：

| 个数 | 走什么 | 谁 |
| --- | --- | --- |
| 10 | `/navigate_through_poses` | `go.py`、`end_3_12.py`、`goal_l.py`、`detct.py`、`一条路径+csv.py`、`人工智能赛道初始版本代码.py`、`第二个点nav2打断，然后转圈圈.py`、`终点不停.py`、`跑一圈以内.py`、`go一条路径.py` |
| 3 | `/follow_waypoints`（**一圈一圈跑**） | `重复载入完成圈数.py`、`会跳过几个点.py`、`ai_第一个点后回终点.py`，额外认一个 `--laps N` |
| 2 | `nav2_simple_commander` 的 `BasicNavigator` | `go_2c.py`、`go copy_多点需要切换.py` |
| 1 | `/goal_pose`（不是 action） | `go copy.py` |
| 1 | 只订阅（按 `f` 存点） | `get.py` |

`FollowWaypoints` 是「一个航点一个航点地判到达」那条路，`number_of_loops` 就是圈数；
为什么不能用「把同一串航点再发一次」做第二圈，见下面坑列表里那条。
航线是一条四列 CSV（`x`、`y`、四元数的 `z`、四元数的 `w`）。`sim_room12.csv` 是仿真器那间
12 m 房间，`ai_test*.csv` 是几条赛道航线 —— 仓库里**没有** `test.csv`，车上原本有那一条。

**读哪一条由 `waypoint_file()` 说了算**（13 个脚本有它，其余 4 个 —— `get.py`、
`go copy_多点需要切换.py`、`go_2c.py`、`go一条路径.py` —— 读死一条航线，或者把航点直接写在代码里）：

1. 命令行里第一个 `.csv` 参数，相对脚本目录解析（绝对路径原样用）；
2. 没有参数就按 `test.csv`、`ai_test.csv` 的顺序，取脚本目录里第一个**存在**的。

所以 `run go.py sim_room12.csv` 和 `run go.py` 都对；车上把 `test.csv` 放回原位，行为就还是原来的
（它在第一顺位）。**这不是「补齐了车上那条航线」**，只是「读不到不再抛 `FileNotFoundError`」。

车上的用法是 `ros2 run racecar <脚本>`，所以 `src/racecar/CMakeLists.txt` 现在**17 个脚本和 4 条 CSV
都装**（以前只装 `get.py` / `detct.py` / `go.py` / `goal_l.py` 四个，CSV 一个都不装，路上找不到航线）。

`get.py` 是个例外：它不导航，而是订阅 `/goal_pose`，你按 `f` 就把收到的点存到 `out_test.csv`。
键盘那条路按平台分：Windows 走 `msvcrt`，Linux 走 `termios` / `tty`。

## 几个必须记住的坑

* **`slam` 会重启 gRPC 守护进程**：shim 的一个话题只能有一个 writer，而 slam_toolbox 也要发 `/map`，
  所以建图时守护进程改成只镜像 `/clock /odom /scan /tf /tf_static`；跑完用 `mirror-all` 恢复。
* **建图时导航要加 `--no-amcl`**：此时地图来自 slam_toolbox，`map -> odom` 由它发布。
* **`nav --map NAME` 读的是快照**：`run_sim.py --seed` 默认不给，所以每次重启仿真器都是一间
  新房，`map/truth_room12` 这类文件只对应某一次采摘（实测差 15.5% 的像素）。仿真里要用**当前**
  这间房就写 `nav --map live`：它先镜像仿真器的 `/map`（话题本身就是真值）、当场存成
  `map/truth_live.yaml`，再按 `nav` 那套镜像把 `map_server` 立成 `/map` 唯一的写者。
* **仿真器重启之后必须重启整栈**：栈里每个 `use_sim_time` 的节点握的都是仿真时钟的**绝对时刻**
  （`Wait` 行为的 `wait_end_`、AMCL 的超时、控制器的提前模拟窗口）。仿真器一重启，`/clock` 的纪元
  从头开始，那些绝对时刻就落到未来几小时 —— 恢复分支里那个 5 秒的 `Wait` 永不返回，`bt_navigator`
  卡在「processing」，之后每个 goal 都被拒（现象就是 `BasicNavigator` 打
  `NavigateToPose goal to ... was rejected!`）。顺序永远是 `stop` → 重启仿真器 → `nav`。
  链条与实测见 `bridge/docs/04-gaps.md` 第二十八节第十一条。
* **`NavigateThroughPoses` 的成功只看路径的终点**：`nav2_controller` 的 goal checker 比的
  **只是路径终点**（`config/nav.yaml` 里 `xy_goal_tolerance` 是 1.0 m），中途那 8 个航点走没走过
  它一次都不看。两种现场都出自这一条：终点压在车轮底下（首尾相接的 CSV）会立刻 `Goal succeeded`；
  **重发同一条航线**也一样 —— 车正停在上趟的终点上，第二个 goal **0.11 s** 就回「成功」，
  车一步不动（实测见 `bridge/docs/03-validation.md` 证据 78）。
  所以「靠重复发目标做闭环」是不通的：圈数要走 `/follow_waypoints`
  （`nav2_msgs/action/FollowWaypoints`，它一个航点一个航点地做，`number_of_loops` 就是圈数）——
  同一条九点航线重发，走 `FollowWaypoints` 第二趟照样真跑 110.6 s、9 个航点全过。
* **跑脚本之前那个 `/sim/reset` 不是可选的**：不 reset 就发 goal，车还停在上一趟的终点上，
  上面那条就会发生 —— 实测 0.09 s 回 `error_code=0`、9 个航点一上来全是 `waypoint_status=1`，
  车一动没动。reset 把车送回起点，第一个 goal 才是真的。
* **`终点不停.py` 要的 `stop` 服务这套兼容层里没有**：它现在只问一次（5 s 上限），问不到就记下来、
  打一条 `WARN`、继续跑。改之前是 `while not stop_client.wait_for_service(...)`，而调用点在**反馈
  回调**里 —— 服务不在就是无限循环。
* **航点的朝向曾经被写丢（12 个脚本）—— 已修**：CSV 第 3、4 列是四元数的 z/w，这 12 个原来写的是
  `pose.pose.position.z = point["z"]` / `pose.pose.orientation.w = point["w"]`，于是朝向的 z 一直是 0、
  那个正弦值进了地面机器人上恒为 0 的 `position.z`。**2026-09-19 量过也改掉了**：同一个航点
  （朝向要 +115.02°）旧写法收在 **-55.65°**（差 170.67°、17.5 s），新写法收在 **+61.14°**
  （差 53.87°、3.4 s，落在 `nav.yaml` 那两个 1.0 rad 容差里）。`go copy.py` 写的就是对的那一行。
  见 `bridge/docs/04-gaps.md` 第二十八节第 12 条、`bridge/docs/03-validation.md` 证据 80。
* **`FollowWaypoints.number_of_loops` 从 0 数起**，给 N 就是走 **N+1** 遍
  （`nav2_waypoint_follower/waypoint_follower.cpp:357-372`），所以脚本里是
  `goal.number_of_loops = laps - 1`。它的 feedback 里**没有** `distance_remaining`，只有
  `current_waypoint`，想按距离做判断得换别的入口。
* **`NavigateThroughPoses` 的 goal 字段两边不是一个定义**：兼容层里（移植的 nav2 1.5.1）是
  `nav_msgs/Goals poses`，航点在 `goal.poses.goals`；车上（Jazzy）是
  `geometry_msgs/PoseStamped[] poses`，直接就是那串航点。所以脚本里写成
  `if hasattr(goal_msg.poses, "goals"):` 两分支 —— 只写一种就只在一个平台上跑得起来
  （车上第一次 `ros2 run` 就是 `AttributeError: 'list' object has no attribute 'goals'`）。
  两边各跑一次的探测脚本是 `.scratch/probe_ntp_poses.py`，全貌见
  `bridge/docs/04-gaps.md` 第二十八节第 19 条、证据 82。
* **`/amcl_pose` 静止时永远不更新**：AMCL 默认 `update_min_d` / `update_min_a`，车不动就不发新位姿
  —— 拿它当「车现在在哪」会一直拿到 0 条。要看车在哪用 `/odom`（`map` 帧的起点压在 `odom` 上）。

## src/ 里没进兼容层的包

这四个是**车上用、兼容层里没有对应实现**的，原样留着：

| 包 | 是什么 | 在这套兼容层上 |
| --- | --- | --- |
| `src/slam_gmapping` | gmapping 的 ROS2 封装（5 个文件，一薄层） | 没有 |
| `src/openslam_gmapping` | gmapping 本体（118 个文件，上游 vendor） | 没有 |
| `src/lslidar_driver` `src/lslidar_msgs` | 镭神雷达驱动与消息（纯 C++/CMake） | 没有；这里的 `/scan` 来自对端 |
| `src/README.md` | 其实是 `lslidar_driver` 的 README | 位置不对，没动它 |

**所以「用哪套 SLAM」在这棵树里是分后端的**：`gmapping.bat` / `gmapping.sh` 在 `shim` 后端跑的是
**slam_toolbox**（`config/slam_toolbox.yaml`），在 `ros2` 后端才真的
`ros2 launch slam_gmapping slam_gmapping.launch.py`。想在车上也换 slam_toolbox，
参数文件已经在包里了。

## 搬走了什么

| 搬走的东西 | 现在在哪 |
| --- | --- |
| `car.sh`、`racecar_init.sh`、串口 udev 规则 | `../rubblish/racecar/` |
| `racecar_driver`、`encoder`、`hipnuc_imu`、`serial-foxy` | `../rubblish/racecar/src/` |
| 旧底盘节点 `.cpp`、`Run_car.launch.py`、`ekf.launch.py`、EKF 配置 | `../rubblish/racecar/chassis_legacy/` |
| 摄像头配置与视觉脚本（`detct copy.py`、`hsv_show.py`、`record.py`） | `../rubblish/racecar/chassis_legacy/` |
| 键盘遥控 `racecar_teleop.py`（发 `/teleop_cmd_vel`，PWM 约定，Windows 上 import 就挂） | `../rubblish/racecar/chassis_legacy/scripts/` |
| AI 推理 / 相机 / 性能测试那一整套 `code/` | `../rubblish/code/` |
