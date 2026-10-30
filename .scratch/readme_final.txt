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

## 航点脚本和它们的航线

`src/racecar/scripts/` 里 17 个脚本都是**客户端**：发一个 `NavigateThroughPoses` 目标，本机的 nav2
回结果。航线是一条四列 CSV（`x`、`y`、四元数的 `z`、四元数的 `w`）。`sim_room12.csv` 是仿真器那间
12 m 房间，`ai_test*.csv` 是几条赛道航线 —— 仓库里**没有** `test.csv`，车上原本有那一条。

**读哪一条由 `waypoint_file()` 说了算**（8 个脚本有它，其余 9 个读死 `ai_test.csv` / `ai_test2.csv`，
或者把航点直接写在代码里）：

1. 命令行里第一个 `.csv` 参数，相对脚本目录解析（绝对路径原样用）；
2. 没有参数就按 `test.csv`、`ai_test.csv` 的顺序，取脚本目录里第一个**存在**的。

所以 `run go.py sim_room12.csv` 和 `run go.py` 都对；车上把 `test.csv` 放回原位，行为就还是原来的
（它在第一顺位）。**这不是「补齐了车上那条航线」**，只是「读不到不再抛 `FileNotFoundError`」。

车上的用法是 `ros2 run racecar <脚本>`，所以 `src/racecar/CMakeLists.txt` 现在**17 个脚本和 4 条 CSV
都装**（以前只装 `get.py` / `detct.py` / `go.py` / `goal_l.py` 四个，CSV 一个都不装，路上找不到航线）。

`get.py` 是个例外：它不导航，而是订阅 `/goal_pose`，你按 `f` 就把收到的点存到 `out_test.csv`。
键盘那条路按平台分：Windows 走 `msvcrt`，Linux 走 `termios` / `tty`。

真车 / Ubuntu（`ros2` 后端）：

```bash
source ~/Desktop/racecar/install/setup.bash
bash ~/Desktop/racecar/nav.sh
```

每个子命令的完整选项在 `tools/racecar_ctl.py --help` 里。

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
* **`NavigateThroughPoses` 的最后一个航点不能是起点**：`nav2_controller` 的 goal checker 比的是
  **路径的终点**，终点压在车轮底下（`xy_goal_tolerance` 是 1.0 m）会立刻报 `Goal succeeded` 而车
  一动不动。闭环要靠重复发目标，不能靠一条首尾相接的 CSV。

* **跑脚本之前那个 `/sim/reset` 不是可选的**：不 reset 就发 goal，上一趟的「已过」状态还在，
  `RemovePassedGoals` 会把整条航线一次性判成完成 —— 实测 0.09 s 就回 `error_code=0`、9 个航点
  一上来全是 `waypoint_status=1`，车一动没动。
* **`终点不停.py` 要的 `stop` 服务这套兼容层里没有**：它现在只问一次（5 s 上限），问不到就记下来、
  打一条 `WARN`、继续跑。改之前是 `while not stop_client.wait_for_service(...)`，而调用点在**反馈
  回调**里 —— 服务不在就是无限循环。
* **航点的朝向被写丢了（12 个脚本）**：CSV 第 3、4 列是四元数的 z/w，但这 12 个写的是
  `pose.pose.position.z = point["z"]` 和 `pose.pose.orientation.w = point["w"]` —— 朝向的 z 一直是 0，
  那个正弦值进了地面机器人上恒为 0 的 `position.z`，于是到点时的车头方向由规划器定、不由 CSV 定。
  同目录的 `go copy.py` 写的是 `orientation.z = ...`，所以**原意是清楚的**；这是车上原有的写法，
  **这一轮没动它**（改了每个航点的收尾朝向都会变，得先在车上量一次）。见
  `bridge/docs/04-gaps.md` 第二十八节第 12 条。

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

