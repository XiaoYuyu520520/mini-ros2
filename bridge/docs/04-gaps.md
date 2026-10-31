# 04 · 这个项目还缺什么

这一页只讲**缺口**。已经能跑的东西在 `03-validation.md` 里有证据，这里不重复。
每条写三件事：**现在是什么样**、**缺什么**、**缺了会有什么后果**。
证据用两种标记：**【查】**＝对着当前源码 / 二进制 / 配置文件核过（写明位置）；
**【跑】**＝本轮真的跑过（写明命令或产物）。

上一版这一页只覆盖 nav2 与 SLAM。现在按「整个项目」的口径重写：兼容层、跨机、
对端真实性、工程化各有一节；nav2 与 SLAM 两节保留原来的判断，只修掉已经过期的地方
（`nav2_graceful_controller` 和 `vo_server` 当时是「编好了、没接线 / 没跑过」，
不再是「没有移植」；**这两条现在也做完了**，见证据 34。这一轮又补上了 PNG coder，
见证据 35）。

---

## 一句话结论

三条验收（Windows 当 ROS2 用 / Windows 发布、Ubuntu 订阅 / nav2 建图规划指挥车）**已经成立**。
还缺的东西按「现在挡着什么」排：

1. ~~**兼容层没有参数子系统**~~ **已经补上**（证据 33）：`rclcpp/parameter_service.hpp`
   从空壳（原文：*Parameter services are not exposed in the shared-memory shim*）换成了六个
   真正的 handler，`NodeOptions::start_parameter_services()` 也不再是死开关，
   `ros2 param list / get / set / describe / dump` 因此能对着一个**正在跑**的 nav2 节点读和写
   （`/controller_server`、`/bt_navigator`、`/amcl` 都验过）。同一轮里把 `/parameter_events`
   也补上了（证据 39）：节点自己发，另一个进程读得到。但它只在**本机**的共享内存里，
   跨机过不了桥（见 B 节与 G 节）。
2. ~~**没有话题 remap**~~ **已经补上**：`-r FROM:=TO`（证据 32）。已经编好的
   `velocity_smoother` / `collision_monitor` 因此从「旁路观察」变成了 `nav2_stack.bat --safety`
   这条真正的链路；还缺的只是 launch 那套「一份文件声明一组 remap + 参数覆写」的语法。
3. **跨机只有一张编解码表**：38 种话题类型、38 种 service、**20 个 action**（2026-09-17 深夜把
   nav2 的 19 个 action 全补上，证据 61 / 第二十三节）；表外的类型过不去，带数组字段的 goal
   （`SmoothPath` / `FollowWaypoints` / `NavigateThroughPoses`）也没有命令行入口。
4. **对端从来不是真 ROS2**：Ubuntu 那台机器上没装 ROS2，`ros2_bridge_sim` 是按 proto 自己写的
   Python 对端；SDK 的 `RclcppBackend`（`ROS2_GRPC_SDK_WITH_ROS2=ON`）**从没编译过**。
   跨机链路验的是「和我们的仿真对得上」，不是「和 ROS2 对得上」。
5. **nav2 上游还差 7 个包**（`nav2_theta_star_planner`、`nav2_constrained_smoother`、
   `nav2_following`、`nav2_loopback_sim`、`nav2_system_tests`、`nav2_rviz_plugins`、
   `nav2_simple_commander`）；SLAM 侧只差 `merge_maps_kinematic.exe` 的**合并语义**这一件 ——
   **PNG coder、位姿图存档（`serialize_map` / `deserialize_map`）、以及那两个服务的类型通路都已经做完**
   （证据 35 / 41 / 51：2026-09-17 起 `add_submap` / `merge_submaps` 也在活节点上真调过了，
   剩下的是「不给 GUI 怎么喂子图位姿」）。

工程化那一层（版本控制 / CI / 安装包）目前是**零**。它不挡功能，挡的是「别人能不能复现」。

---

## 现在能用什么（基线）

| 层 | 已经在跑 |
| --- | --- |
| 建图 | `slam_toolbox` 2.8.5 移植到 Windows（sync / async / localization / merge_maps 四个 exe，`/slam_map`），跟踪误差 0.000 m / 0.0° |
| 定位 | `nav2_amcl` 1.5.1（粒子滤波 + 光束/似然场传感器模型），`amcl_bridge_node` 发 `map -> odom` |
| 地图 | `nav2_map_server` + PGM / PNG / YAML 读写（绕开 GraphicsMagick）、`map_saver_cli.exe`、内存图；存下来的图由 `map_server.exe` 读回来，与源文件逐字节相同（证据 35 / 36） |
| 参数 | `ros2 param list / get / set / describe / dump` 对着**正在跑**的节点读写（`<node>/get_parameters` 等六个服务 + on-set 校验回调），同机调用不经过桥（证据 33） |
| 全局规划 | NavFn（`smoother` 前的经典图搜索）与 **Smac Hybrid-A***（自实现的 OMPL 子集）；Smac 在「直线不通」的目标上真绕过障碍（证据 67） |
| 局部控制 | 同一个栈里并存四个能被 `/controller_selector` 选中的控制器（`controller_plugins: ["FollowPath", "DWBFollowPath", "MPPIFollowPath", "GracefulFollowPath"]`，启动日志里四个都打印了 `Created controller`，都能通过 `lifecycle_manager` 到 ACTIVE）：`nav2_regulated_pure_pursuit_controller`（默认，挂在 `FollowPath` 上）、`nav2_dwb_controller`（`dwb_core` + `dwb_plugins` + `dwb_critics`，证据 27）、**`nav2_mppi_controller`**（`MPPIController` + `DiffDriveMotionModel` + 8 个 critic + `DefaultOptimalTrajectoryValidator`，证据 30）。还有 `nav2_rotation_shim_controller`（先把车原地转向，再交给内部的 RPP）也编好了，但它占的就是 `FollowPath` 这个 id，要用自己那份参数文件起栈 —— A/B 见证据 25；`GracefulFollowPath` 那一路的 A/B 见证据 34 |
| 自救行为 | `nav2_behaviors` 的 `behavior_server`：`Spin` / `BackUp` / `DriveOnHeading` / `Wait` / `AssistedTeleop` 五个插件全部创建并激活，`spin` / `wait` / `backup` 各真跑过一次（证据 21） |
| 路径平滑 / 多点 | `smoother_server` + `waypoint_follower` 被同一个 `lifecycle_manager` 拉到 ACTIVE，并由 `nav2_waypoints.exe` 真正驱动：`ComputePathToPose` → `SmoothPath` → `FollowWaypoints` 连跑 6 次全 `SUCCEEDED`、`missed_waypoints` 全 0，最尖的拐角 45.0° → 4.9°（证据 22） |
| 安全（旁路 / 进链） | `nav2_velocity_smoother` + `nav2_collision_monitor`。`nav2_safety.bat` 是旁路观察（`/cmd_vel` → `/cmd_vel_smoothed` → `/cmd_vel_monitored`，两条输出都没有第二个写者、不动车）；`nav2_stack.bat --safety` 是**进链**（`controller_server` / `behavior_server` 被 remap 到 `/cmd_vel_nav`，smoother 跟过去，monitor 的 `cmd_vel_out_topic` 指回 `/cmd_vel`，成为它的唯一写者）（证据 23 / 32） |
| 行为树 | `nav2_behavior_tree` 节点注册表 + `bt_navigator`（navigate_to_pose / navigate_through_poses 两棵树） |
| costmap | static / obstacle / voxel / inflation / keepout_filter / speed_filter 全部激活成功 |
| 时间 | `use_sim_time` + `/clock`（四个节点都跟机器人的仿真时钟，证据 19） |
| 自主回充 / 路网 | `docking_server`（`opennav_docking::SimpleChargingDock`）与 `route_server`（GeoJSON 图加载器 + `DistanceScorer` / `CostmapScorer` / `AdjustSpeedLimit` / `ReroutingService` / `CollisionMonitor`）都能被 `nav2_docking_route.bat` 拉到 ACTIVE；**没有 dock 实例、也没有图文件**，所以只到「装得起来」（证据 28） |
| 可视化 | `viz.exe` + `viz_feed.exe`：地图 / 扫描 / 路径 / 位姿 / Marker / 代价地图 / 点云，2D Goal 与 2D Pose Estimate；环境加载后命令名就是 `rviz2`。端到端验过（见 [`03-validation.md`](03-validation.md) 的 viz 一节） |
| 跨机 | 话题（双向）、**38 种 service 类型**（远端在跑 7 个；`slam_toolbox` 的全部 11 个 srv 与 `nav_msgs/srv/GetMap` 是本机方向；nav2 自己的 20 个 `nav2_msgs` srv 见证据 52 / 53）、**20 个 action codec**（nav2 的 19 个全在；远端在跑 5 个）、`/clock`、`/tf` / `/tf_static`（证据 24） |

---

## A. nav2：上游 46 个包，还缺 7 个

上游 `nav2_extracted/navigation2-1.5.1` 里有 46 个 `package.xml`。当前构建覆盖情况**【查】**
（`nav2\ros2_shim\CMakeLists.txt` 的 25 个 `nav2_port_package(...)`，加上直接编译的
`nav2_amcl` / `nav2_behavior_tree` / `nav2_util_core` / `nav2_voxel_grid` / `nav2_ros_common`，
加上只出消息的 `nav2_msgs` / `dwb_msgs` / `nav_2d_msgs`）：

| 还缺的包 | 现在是什么样 | 缺了会有什么后果 |
| --- | --- | --- |
| `nav2_theta_star_planner` | 没有移植，`build` 目录里 0 个产物 | 全局规划器少一种（上游可选插件之一）；对当前链路无影响 |
| `nav2_constrained_smoother` | 没有移植 | 平滑器少一种（要平滑度约束时用不了）；`smoother_server` 现在的 `SmoothPath` 仍然可用（证据 22） |
| `nav2_following`（`opennav_following`） | 没有移植 | 「跟人 / 跟车」整个功能不可用（对本项目目标优先级最低） |
| `nav2_loopback_sim` | 没有移植 | 没有「上游自带的仿真机器人」这个替代品；本项目的仿真在 Ubuntu 侧（见 D 节） |
| `nav2_system_tests` | 没有移植 | 上游那套系统集成测试跑不了；本项目的回归是自己那三套（22 + 4 + 52） |
| `nav2_rviz_plugins` | 没有移植；Windows 上的可视化由 `viz` 顶上（**不是**这个插件） | rviz 插件里的调参 / 交互面板没有；日常看地图 / 扫描 / 规划 / 位姿 / 代价地图 / 点云、发目标点与初始位姿已经由 `viz` 覆盖 |
| `nav2_simple_commander` | 没有移植（纯 Python，要 rclpy） | 没有 Python API；发目标只能用 C++（`nav2_goal.exe` / `nav2_waypoints.exe`）或 CLI |

不需要单独移植的（**不是缺口**）：`nav2_core` 与 `opennav_docking_core` 只有头文件，
CMake 用 `GLOB ${NAV2_SRC}/*/include` 全部挂上了；`nav2_util` 的源码被收进 `nav2_util_core`
目标；`nav2_msgs` / `dwb_msgs` / `nav_2d_msgs` 由生成器出消息；`nav2_bringup` / `nav2_common` /
`nav2_dwb_controller` / `navigation2` 是元包与配置包（**注意**：上游 `nav2_bringup` 里真正被用的是
`params/nav2_params.yaml` —— `nav2_stack.bat` / `nav2_safety.bat` / `nav2_docking_route.bat` 都把它当
第一个 `--params-file` 传给每个节点；没被用的是 `launch/`，那一套语法本兼容层没有。
Windows 侧只是在这个文件之上再叠一层自己的覆写：
`nav2\ros2_shim\config\nav2_shim_overrides.yaml`）。

### Windows 可视化（`viz`）：不是 rviz2 的移植

`viz` 是按「看得见 + 点目标」做的工具，不是把 rviz2 搬过来：没有 Qt / OGRE / 插件体系。
数据面 `viz_feed.exe` 是一个普通的 shim `rclcpp::Node`，带 `tf2_ros::Buffer` 与
`TransformListener`，每条消息在边车里变换到视口的固定坐标系后再交给 Go/Fyne 界面。
它注册成订阅者，所以 nav2 才会发 `/plan`、`/cmd_vel`；它自己持有 `/goal_pose` 与
`/initialpose` 的写端（**一个通道一个写者**）。

仍然开着的边：

| 边 | 现状 |
| --- | --- |
| 真 ROS2 的 QoS | **已量（含负对照）**：Jazzy 对端 best_effort 的 `/qos_probe/scan` 与 transient_local 的 `/qos_probe/map` 都收到；把 sensor 改回 reliable 之后扫描一条都不到。见 `03-validation.md` 第三节 |
| 图层 | 地图 / 扫描 / 路径 / 位姿 / Marker（`Marker` 与 `MarkerArray`，含箭头）/ 代价地图（`Costmap` 与增量更新）/ 点云（`PointCloud2`，按高度上色）。URDF 与 3D 仍然没有 |
| 发目标 | v1 只发 `/goal_pose`，不走 `NavigateToPose` action，所以没有进度反馈与中途取消 |
| 逐话题 QoS | proto 里预留了 `QosProfile`，daemon 还没透传 |

### `nav2_graceful_controller`：已经从「选不到」改成「能选、跑过」

| 缺口 | 现状 | 缺什么 | 代价 |
| --- | --- | --- | --- |
| ~~**`nav2_graceful_controller`**~~ | **做完了**（证据 34）：`nav2_shim_overrides.yaml` 的 `controller_plugins` 变成四个 id，多了一个 `GracefulFollowPath:` 参数块；重启栈之后日志里是 `Controller Server has FollowPath DWBFollowPath MPPIFollowPath GracefulFollowPath  controllers available.`，同一个目标上还跑过一轮 A/B（两边都 `SUCCEEDED`）。**代码一行没改** —— pluginlib 用的注册名就是那个 id，`controller_server` 的链接行本来就整档收了它 | 没有了 | 剩下的是覆盖度：`nav2_rotation_shim_controller` 与 `FollowPath` 抢同一个 id，只能按参数文件切（证据 25）；A/B 的数字**不能**读成「graceful 更好」，理由写在证据 34 里 |

### 速度平滑与碰撞监控：已经从旁路接进驱动链

remap 补上之后（证据 32），这两个节点有两种接法，由 `nav2_stack.bat` 的 `--safety` 决定：

* `nav2_safety.bat`（旁路，`--safety` 不写时的样子）：`/cmd_vel_smoothed`、
  `/cmd_vel_monitored` 有人写、没人订阅，小车仍然听 `controller_server` 直接发的 `/cmd_vel`
  —— 这一层**不改变车的命运**，用来量测与观察。
* `nav2_stack.bat --safety`（进链）：`controller_server` / `behavior_server` 被 remap 到
  `/cmd_vel_nav`，smoother 跟过去，`collision_monitor` 的 `cmd_vel_out_topic` 指回 `/cmd_vel`，
  于是 `/cmd_vel` 在本机的写者只剩它一个。停止多边形放大到 12 m 的同一轮里，同一个目标从
  「走 2.39 m」变成「走 0.00 m」—— 这一层**就是车的命运**。

还没有证据的部分：`-r` 只能写在每个节点的命令行上（没有 launch 文件那套「一组 remap +
参数覆写」的声明）；`/collision_monitor_state`（`nav2_msgs/msg/CollisionMonitorState`）
**已经进了编解码表**（2026-09-17，证据 48：`ros2 topic echo` 打得出 `action_type: 2 (slowdown)`
与 `polygon_name: "StopPolygon"`）；`slowdown` / `limit` / `approach` 三种
action_type 与 `pointcloud` / `range` 两种 source 一次都没跑过 —— `nav2_shim_safety.yaml`
里只有 `scan` + 一个 `stop` 多边形。

### docking / route 只到「装得起来」

`docking_server` 与 `route_server` 已经被拉到 ACTIVE（证据 28），但两个都没有数据：
仓库里没有 dock 实例配置，也没有 `.geojson` 路网文件【查：全仓库搜 `*.geojson` 只有消息定义】。
`ComputeRoute` 会答 `No graph set! Aborting request.`，跟随/回充的端到端动作一次都没走过。

### ~~SMAC Hybrid-A* 的 `std::bad_alloc`~~：**已修** —— 角度箱越界，不是启发式缓存也不是膨胀半径（2026-09-19）

这一节 2026-09-18 记的两个 workaround（`cache_obstacle_heuristic: False`、
`global_costmap.inflation_radius: 0.45`）**都建立在错因上**，2026-09-19 把真正的分配点用 gdb
钉住、修掉，两个配置一起回到上游/真车取值。

现场：对一个到不了的 goal（`truth_live` 上 `start=(0.79, 0.50, yaw=-0.95)` →
`goal=(1.00, 1.50, yaw=0)`）反复规划。第一次是正常的失败（~770 ms 走到 `NoValidPath`），
**第二次起每次 47 ms 抛 `std::bad_alloc`**，planner_server 里成片 `Aborting handle`。
gdb（`D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin\gdb.exe`，catchpoint 挂 `__cxa_throw`）
给出的调用链：

```text
#0 __cxa_throw
#1 operator new(unsigned long long)                     <- 就是 operator new 抛的
#2 GridCollisionChecker::inCollision(float,float,float,bool)
#3 NodeHybrid::isNodeValid(bool, GridCollisionChecker*)
#4 AnalyticExpansion<NodeHybrid>::getAnalyticPath(...)
#5 AnalyticExpansion<NodeHybrid>::tryAnalyticExpansion(...)
```

`inCollision` 里那次 `new` 是给足迹用的：`oriented_footprints_[angle_bin]` 越界之后读到的
`size()` 是那段内存里随便什么数，`reserve()` 于是要几个 GB。四处越界，全在
`nav2/nav2_extracted/navigation2-1.5.1/nav2_smac_planner/`：

| 位置（修前的行号） | 原来 | 现在 |
| --- | --- | --- |
| `include/nav2_smac_planner/analytic_expansion_impl.hpp`（254-257） | 插值出的绝对 yaw 只做**一次** `+2π` / `-2π` 归一化 | `fmod(θ, 2π)`、负数再补 2π；三个 `reals[i]` 只要非有限就放弃这次展开 |
| 同上（269-270） | `angle` 原样写进 `proposed_coordinates`，越界的值成为节点 pose | 写进去的已经是规约过的角度 |
| `src/collision_checker.cpp`（144） | `oriented_footprints_[angle_bin]` 直接下标 | `fmod` 规约 + `min(…, n-1)`，真的越界时 `WARN_THROTTLE` 把实参报出来（**这是止血点**：上游怎么造出坏 bin 都不再越界） |
| `src/node_hybrid.cpp`（304-313） | `new_heading` 只减一次、`delta_xs[i][node_heading]` 拿 float 当下标 | `fmodf` 规约 + 下标做边界判断 |

越界是谁造出来的：`analytic_expansion_impl.hpp` 里那段「把转弯半径从 `min_turning_radius`
每次 +0.5 一直摸到 4 倍」的细化循环（修前 417-453 行）会用新的 `DubinsStateSpace` 重新插值，
`num_intervals ≈ 长度/√2` 相当大，而 `i / num_intervals` 是 `double` 除 `unsigned`、步长精确到
1/42.4 —— 起终点几乎反向（夹角 ≈ π）时中间航向对步长极敏感，一次插值就能给出 > 2π 的 yaw。
这段代码是上游 1.5.1 的原样，不是移植引入的。

修后实测（同一份地图、同一批 9 个目标、只重启栈）：

| 量 | 修前 | 修后 |
| --- | --- | --- |
| 8 个正常目标 | 9~29 点，15~32 ms | 逐条同结果、同耗时 |
| `(1.00, 1.50)` 这个坏目标 | **47 ms 抛 `std::bad_alloc`** | **1.7~1.9 s 报 `PlannerTimedOut: "exceeded maximum iterations"`**（error_code 207） |
| 坏目标连跑 6 次 | 6 次都崩 | 6 次都是同一个超时；后面紧跟的好目标照样 16 点成功 |
| `bad_alloc` 行数 | 每次规划都有 | **0** |

`(1.00, 1.50)` 本身是**坏目标**，不是规划器缺陷：global costmap 实测该格 cost=138，它上方
`y∈[1.60,1.70]` 是 253（内切带）、`y≥1.75` 才是 254（墙）；0.64x0.36 的车体在 yaw=0 时
footprint 的最大 cost 正好 253 < `OCCUPIED_COST` 254，所以**目标格合法**，但车体挤不进这
0.2 m 的缝：同一个目标换个起点（`(0.70, 1.50)`）要绕 44 点、0.17 s 才走通，起点回到
`(0.79, 0.50)` 就永远搜不到，于是用满 `max_iterations: 1000000`。x ≥ 1.25 的同类目标全部
15~31 ms 通过。

两个配置因此都回滚，`nav.yaml` 里那两处注释也跟着重写了：

* `cache_obstacle_heuristic` 回到上游默认 `true`：`true` / `false` 的 9 个目标逐条同结果，
  `true` 还省掉每次规划重算一遍障碍启发式（同一个到不了的 goal 1.66~1.72 s vs 1.87~1.97 s）。
* `global_costmap.inflation_radius` 回到与真车共用的 `0.25`：与 `0.45` 逐条同结果、逐条同耗时
  （15~32 ms）。代价是上游那两条 `computeCircumscribedCost` ERROR 会一直打 —— 那条慢路径是
  上游为「膨胀半径 < 外接半径」设计的降级，不是缺陷。

现场与全部数字见 `03-validation.md` 证据 72。

---

## B. 兼容层（`ros2_shim`）：ROS2 的表面还缺哪些

**【查】** `nav2\ros2_shim\include` 的顶层目录就是这张清单：`rclcpp`(36 个头)、`rclcpp_action`、
`rclcpp_lifecycle`、`tf2` / `tf2_ros` / `tf2_geometry_msgs` / `tf2_sensor_msgs`、`pluginlib`、
`message_filters`、`point_cloud_transport`(桩)、`diagnostic_updater`、`bond` / `bondcpp`、
`interactive_markers`(部分)、`ament_index_cpp`、`rclcpp_components`(只有 `register_node_macro`)、
`Poco` / `tbb` / `angles` / `uuid` / `Magick++`(PGM + 8 位 PNG 替身)。

| 缺口 | 现状 | 缺了会有什么后果 |
| --- | --- | --- |
| **参数服务** | **已有**：`/node/get_parameters`、`/get_parameter_types`、`/set_parameters`、`/set_parameters_atomically`、`/describe_parameters`、`/list_parameters` 六个服务由 `rclcpp/parameter_service.hpp`（声明）与 `rclcpp/node.hpp`（实现）提供（证据 33）；`NodeOptions::start_parameter_services(false)` 是真开关（`nav2_ros_common::generate_internal_node()` 会关掉它，但 bt_navigator 给行为树客户端建的 `*_rclcpp_node` 是**继承父节点 options** 的，服务在，只是树不跑时它不被 spin）；`set_parameters` 会走 on-set 校验回调，所以 nav2 自己的 `ParameterHandler::validateParameterUpdatesCallback` 拦得住非法值，并在节点日志里留一行。`ros2 param list` 的节点名与上游一样**可以省略** —— 不写就遍历图里每个节点、逐段打印，没有参数服务的节点跳过（证据 33）。**`/parameter_events` 也发了**（证据 39）：`Node` 构造时建 publisher，声明进 `new_parameters`、应用成功的 `set` 进 `changed_parameters` | 已经没有「外部读不到 / 改不了运行中节点的参数」这个问题：调参不再需要改 YAML 重启栈，想「不问就知道」也可以订阅 `/parameter_events`（本机的另一个进程读得到）。剩下的边角只剩「参数服务只在本机」——跨机改参数要往 proto 里加 RPC；跨机看事件这一半 **2026-09-17 已经补上**：`rcl_interfaces/msg/ParameterEvent` 进了编解码表（证据 48），跨机端到端尚未实测 |
| **日志话题（`/rosout`）** | **补上了**（证据 50）：`Node` 构造末尾建 `rcl_interfaces/msg/Log` 的 publisher，`RCLCPP_*` 除 stderr 之外再发一条，`file` / `function` / `line` 由宏带上；分流按 logger 名选 sink，`enable_rosout(false)` 或 `NAV2_SHM_ROSOUT=0` 时整个进程不发。但 **`/diagnostics` 仍然没有本机写者** —— 要等 `diagnostic_aggregator` 这类节点被移植 | 没有了「本机没有日志话题」这个问题：nav2 六个服务器自己的话现在能在 `ros2 topic echo /rosout` 里读到。剩下的边角是**容量**：`/rosout` 跟别的消息共用同一条 8 槽环（`NAV2_SHM_SLOT_COUNT` 是全话题共用的一个数）且 volatile，attach 之前的读不到、日志多了会挤掉别的消息；`NAV2_SHM_LOG_LEVEL` 还是 stderr 与话题共用的**一道**闸门。跨机转发走上行白名单能过（证据 50 第九节实测 60/60 条，机器人侧连字段都读得出来）（C 节） |
| **话题 remap** | **已有**：`-r FROM:=TO` / `--remap`，语义按 `rcl_remap_topic_name()` 来 —— 两边都先解析、第一条命中即止、不做链式替换，话题 / 服务 / 动作都走 `Node::resolve_topic()`，`__node` / `__ns` 不走这条规则（`rclcpp/node.hpp` 的 `detail::remapRules()` / `apply_remap()`，测试 `nav2\ros2_shim\tests\remap.cpp`） | 剩下的边角很小：没有「用文件声明一组 remap」的语法，`-r` 只能写在每个节点的命令行上（`bridge\windows\nav2_stack.bat --safety` 就是这么写的） |
| **rclpy** | 没有 Python 绑定。`python\pymessages` 与 `py_interop` 是 ctypes + 生成的消息类，**不是 rclpy** | 依赖 rclpy 的东西全都用不了：`nav2_simple_commander`、上游 `nav2_system_tests`、任何「写个脚本节点」的用法 |
| **rosbag2** | 完全没有 | 不能录、不能放；复现一次跑只能重跑一遍。跨机时也没有「在 Windows 录 Ubuntu 的话题」这条路 |
| **launch / launch_ros** | 没有。启动靠 `bridge\windows\*.bat`（`nav2_stack.bat` / `nav2_safety.bat` / `nav2_docking_route.bat` / `slam_demo.bat` / `amcl_demo.bat`） | 上游的 `launch` 文件用不了（连带 remap、参数覆写、条件启动那一套语法）；换一个组合就要写/改一个 bat |
| **image_transport** | 只有 `point_cloud_transport` 的桩；没有 `image_transport` | `sensor_msgs/Image` 这类相机话题没有传输层，也没有压缩插件 |
| **RViz / rqt** | 没有，也不在计划里 | 「看得见」这件事只能用离线图（`validation\*.png`）或 CLI 顶替 |
| **tf 工具** | `tf2` / `tf2_ros` 库在，`tf_probe` / `tf_listener_probe` 是自带的诊断工具 | 没有 `static_transform_publisher`、`tf2_echo` / `tf2_monitor` 这些命令行工具 |
| ~~**地图图片格式**~~ | **补上了**（证据 35）：`Magick++` 替身现在读 8 位非交错 PNG（灰度 / RGB / 调色板 / 灰度+alpha / RGBA 五种颜色类型、五种行过滤器都实现），写灰度或灰度+alpha；压缩用工具链自带的 zlib（`-static` 让 `libz.a` 跟着进 exe，不新增 DLL）。`bmp` / `jpeg` / 交错 / 16 位 PNG 仍然按名字拒绝 | 没有了：存档不再需要转格式，`slam_toolbox` 默认配置里的 `image_format: png` 能直接用；`bmp` 会像上游在没有那个 delegate 的构建上一样 fallback 成 png |
| ~~**`-p` 的列表值**~~ | **补上了**（证据 47）：`-p name:=[a, b, c]` 以前只认标量 —— `parseParameterValue` 把整串判成一个字符串，节点那边声明的是数组，类型对不上，`Node::resolve_parameter_value` 就**静默**退回声明时的默认值，于是 `-p map_start_pose:=[0.0,0.0,0.0]` 等价于没写。现在 runner 的 `-p`（`lifecycle_runner.hpp`）与节点自己的 `--ros-args -p`（`rclcpp/node.hpp`）都调 `parseParameterText` —— `ros2 param set` 一直在用它；列表的判定同时收紧成「去掉两端空白后整个值就是 `[...]`」（`base[1]`、`C:/maps/a[2]/x.yaml` 这类值保持字符串），类型读不动时补一条 WARN 说出参数名与两个类型；测试 `ros2_shim_param_args`/`tests/param_args.cpp` | 没有了：`-p name:=[...]` 与 `ros2 param set` 现在是同一套语义（同一个解析函数）。遗留一条小差别：整数元素 `[0,0,0]` 对「声明为双精度数组」的参数仍然读不动（现在会 WARN、退回默认值）—— 标量那一层 shim 允许 `int` → `double`，数组这一层没有 |
| **QoS** | 只有声明（可靠信道/历史深度落在本地环形缓冲）。**枚举的拼法 2026-09-17 定论**（以前因为没有 ROS2 只能记成「潜在缺口」）：值一样（两边都指 `RMW_QOS_POLICY_*`），但 shim 写的是 `RELIABLE` / `BEST_EFFORT` / `KEEP_LAST`，上游 `/opt/ros/jazzy/include/rclcpp/rclcpp/qos.hpp` 写的是 `Reliable` / `BestEffort` / `KeepLast`；`nav2_extracted` 里**一个都没直接写枚举名**（`rg "(Reliability|Durability|History)Policy::"` 命中 0，nav2 走的是 `.reliable()` / `SystemDefaultsQoS()` / 参数字符串），所以现状编得过 | 没有 DDS 那套「同一话题不同 QoS 不匹配」的行为 —— 这是设计选择，不是缺陷。真正的遗留是**源码级拼法差异**：哪天有新代码按上游写 `rclcpp::ReliabilityPolicy::Reliable`，这里会编不过；补一组别名是几行的事，没做 |
| **组件 / 组合** | `rclcpp_components` 只有 `register_node_macro.hpp`，没有 component container 的 `load_component` 运行路径 | 上游那种「一个进程装多个组件」的部署方式用不了；这里每个服务是一个 exe |
| **仿真时钟** | `use_sim_time` 只在那几个节点上打开；`rclcpp::utilities::set_use_sim_time()` 仍是空实现 | 没打开 `use_sim_time` 的节点里，「拿 `now()` 配消息戳」的代码会向未来外推 |
| **CLI 面** | 有 `topic`（list / info / type / echo / hz / pub / find）、`service`（list / type / find / call）、`action`（list / type / find / send_goal / cancel）、`param`（list / get / set / describe / dump）、`node list`、`interface list`、`bridge status`【跑：`ros2.bat --help`】 | 取值（`topic pub` / `service call` / `action send_goal` 的最后一个位置参数）2026-09-17 起两种写法都收：真 ROS2 的 YAML（流式与块式缩进都算）与扁平的 `field.path=value`（见第二十六节） | 没有 `ros2 bag`、`ros2 run`、`ros2 launch`、`ros2 lifecycle`、`node info`、`interface show`、`topic bw` / `topic delay`。另外 `service call` 只认编解码表里的类型 —— 2026-09-17 把 nav2 的 20 个 srv 全补上之后（第十八 / 十九节），本机 shm 上调不了的换成 `rcl_interfaces` / `robot_localization` / `map_msgs` / `dwb_msgs` / `geographic_msgs` 那几十条（C 节那张表）；参数读写不受这条限制，`ros2 param` 走的是节点自己那六个服务 |

---

## C. 跨机（`bridge` + `ros2_gRPC_sdk`）：边界是编解码表

**【查】** 三张表都在 `bridge\windows\src\` 里：`type_registry.cpp`（话题）、
`service_registry.cpp`、`action_registry.cpp`。

| 面 | 现在覆盖 | 表外会怎样 |
| --- | --- | --- |
| 话题 | **40 种**：原 19 种（`Twist` / `TwistStamped` / `Odometry` / `LaserScan` / `PointCloud2` / `Imu` / `OccupancyGrid` / `MapMetaData` / `Path` / `PoseStamped` / `PoseArray` / `PoseWithCovarianceStamped` / `PolygonStamped` / `TFMessage` / `Clock` / `String` / `Bool` / `MarkerArray` / `nav2_msgs/Costmap`）**加上 2026-09-17 新增的 19 种**（证据 48）：`PointStamped` / `Polygon` / `nav_msgs/Trajectory` / `Empty` / `Marker` / `DiagnosticArray` / `rcl_interfaces/Log` / `rcl_interfaces/ParameterEvent` / `map_msgs/OccupancyGridUpdate` / `CostmapUpdate` / `CostmapFilterInfo` / `SpeedLimit` / `VoxelGrid` / `ParticleCloud` / `BehaviorTreeLog` / `CriticsStats` / `TrackingFeedback` / `CollisionMonitorState` / `dwb_msgs/LocalPlanEvaluation`，**再加 2026-09-17 深夜的 2 种**（证据 62）：`sensor_msgs/Image` / `sensor_msgs/CameraInfo` | 类型不在表里就**过不去**：`sensor_msgs/JointState` / `CompressedImage`、`std_msgs/Float64`…（本机 shm 上没这个限制：29 个消息包都生成了） |
| service | **38 种**：`slam_toolbox` 的全部 11 个 srv、**`nav2_msgs` 的全部 20 个 srv**、`nav_msgs/srv/GetMap`，加上 `lifecycle_msgs` 的 3 个与 `std_srvs` 的 3 个（2026-09-17 从 9 涨到 38，见第十七 / 十八 / 十九节）；远端 agent 现在实际在跑 7 个 | 表外还有 **45 种**跨不了机：`rcl_interfaces` 的 8 个参数服务、`robot_localization` 的 8 个、`map_msgs` 的 6 个、`dwb_msgs` 的 5 个、`geographic_msgs` 的 4 个……；本机 shm 上没这个限制 |
| action | **20 个**（2026-09-17 深夜补满，证据 61）：`ros2_bridge_sim/action/Drive` + `nav2_msgs` 里**每一个 `.action` 文件**（19 个：Spin / BackUp / DriveOnHeading / Wait / AssistedTeleop / DummyBehavior / NavigateToPose / NavigateThroughPoses / ComputePathToPose / ComputePathThroughPoses / ComputeRoute / ComputeAndTrackRoute / FollowPath / FollowWaypoints / FollowGPSWaypoints / FollowObject / SmoothPath / DockRobot / UndockRobot），每一条都带 Goal / Result 的逐字段打印器（空 feedback 打 `(no fields)`） | 同左；另外**带数组字段的 goal**（`poses` / `viapoints` / `path`）`build_goal` 会明确拒绝，`ros2.bat action send_goal` 发不出去（本机 shm 上 `SmoothPath` / `FollowWaypoints` 已经端到端跑通，证据 22） |

其它跨机缺口：

- **proto 里没有的 RPC**：`ros2_gRPC_sdk\proto\ros2_bridge.proto` 一共 14 个 RPC，全是
  话题 / 服务 / action。没有参数读写（对应 B 节的参数服务）、没有日志转发（本机 `/rosout` 2026-09-17 晚已经有了，证据 50；走上行白名单能过桥 —— `--uplink /rosout` 当天晚上实测机器人侧 **60/60 条一条不差**（连 `level` / `line` 都解出来了）
  （证据 50 第九节）—— 但能过去是因为它走的是通用上行，不是协议里长了一条日志通道）、
  没有 TF 查询、没有 bag。所以「Ubuntu 上直接改 Windows 节点的参数」这件事**连协议都没有**。
- **一次只对一个 agent**：`bridge_daemon.cpp` 的 `connectToAgent()` 是单数，discovery 虽然
  能列出最多 32 个（`config\ros2_gRPC_sdk.yaml` 的 `max_results`），但只连最近的那一个。
  多机器人场景没做。
- **安全**：客户端有 `use_tls` / `tls_root_cert_pem` / `tls_target_name_override` 三个开关，
  但默认明文、**从没验过**（【查】`config\ros2_gRPC_sdk.yaml`）；没有 mTLS / 鉴权 / 压缩。
- **版本不比对**：`Ping` 会带回对端的 SDK 版本，但客户端拿到之后**没有任何检查** ——
  `ros2_bridge_client.cpp` 里 `version()` 只出现在一条日志里【查】。两端版本不一致时不会有提示，
  只会以「某个 RPC 行为不对」的形式表现出来。
- **订阅没有队列，也没有丢弃统计**：客户端的订阅循环是同步的
  （`while (reader->Read(&proto)) { sub->on_message(...) }`），本地消费慢的时候只有 HTTP/2 自己的
  流控兜底；没有背压策略、没有积压/丢弃计数（`throttle_ms` 是唯一一个限速旋钮）。
  也没有开 gRPC 压缩。
- **网络假设**：Ubuntu 要在扫描可达的同一网段内；跨网段要 `--agent host:port`（没有中继/网关）。

---

## D. 对端不是 ROS2（这一条最容易被忽略）

> **2026-09-17 晚：这一节的性质变了。** 机器人上现在装的是**真的 ROS2 Jazzy**
> （`bridge\scripts\ubuntu_ros2_setup.sh`），`ROS2_GRPC_SDK_WITH_ROS2=ON` 的 `RclcppBackend` 编出来
> 并在跑（`bridge\scripts\ubuntu_ros2_peer.sh` 起的 agent，端口 50111），真图上的话题、服务、动作
> 列表都对着它量过（证据 57）。**下面表里前两行的事实已经作废**，留着是为了记住它成立过很久 ——
> 也正因为如此，证据 57 那两处 bug 才躲到了今天：它们**只在真对端上出现**。
> 仿真那两行仍然成立：`ros2_bridge_sim` 还是默认的验收底座，而且它仍然没有动力学与传感器噪声模型，
> `rosbag2` 也仍然缺（B 节）。

| 事实 | 证据 |
| --- | --- |
| Ubuntu 上**没有装 ROS2**，对端是按 proto 手写的 Python 仿真（差分小车 + 合成雷达，7 个话题） | `bridge\ubuntu\ros2_bridge_sim\`（`sim.py` / `agent.py` / `services.py`），复现环境表里 Ubuntu 一行写着「无 ROS2」 |
| SDK 的 ROS2 后端**从没编译过、也没跑过** | `ROS2_GRPC_SDK_WITH_ROS2` 默认 OFF；`build\CMakeFiles\ros2_grpc_sdk.dir\src\server\` 里只有 `mock_backend.cpp.obj` 与 `ros2_bridge_server.cpp.obj`，**没有 `rclcpp_backend.cpp.obj`**【查】 |
| 仿真没有动力学与传感器噪声模型 | `sim.py` 是「订阅 `/cmd_vel` → 按 v/ω 积分 → 发 `/odom` `/scan` `/tf` `/map` `/clock`」，确定性（同种子同命令可复现），但没有 Gazebo / 物理 / 相机 / 深度 / IMU 噪声 |
| 没有 rosbag 录制回放 | 见 B 节 `rosbag2` |

后果：现在这套东西证明的是**协议与兼容层是对的**（CDR 转码有独立的 51 组字节比对，
见「单元与一致性测试」），但**「接到真 ROS2 图上会怎样」没有任何证据**：
真实节点的 QoS、发射频率抖动、消息类型种类、参数服务、`/rosout`（本机现在自己也在发了，证据 50）、
以及最重要的一条 —— `RclcppBackend` 能不能把真实 ROS2 的图正确地暴露成 gRPC。

---

## E. slam_toolbox：缺的是「周边」而不是「核心」

建图主链路是完整的：`sync_slam_toolbox_node` / `async_slam_toolbox_node` 都能起，
Karto 的扫描匹配、位姿图、回环检测、Eigen 求解器都在，归档也做到了逐母子。

而且「建图 → 存图 → 把存下来的图喂回 `map_server`」这一整圈现在有活的证据（证据 36）：
守护进程镜像 `/scan` / `/tf` / `/odom` / `/clock`（**不镜像 `/map`**），`slam_demo.bat`
起 `slam_toolbox` 发布 `/slam_map`，车走一条闭合的圈，同一张 `473 x 473` 的图分别从
`map_dump.exe`、`map_saver_cli.exe --fmt pgm`、`map_saver_cli.exe --fmt png` 落到磁盘，
再交回 `map_server` 读出来 —— 两次往返与源文件逐字节相同。顺带修掉了 `map_dump.exe`
写出来的 PGM 是上下翻的这件事（见 `02-running.md` 第 8 节）。

位姿图存档也在这一轮补上了（证据 41）：`serialize_map` 把位姿图落成 `<name>.posegraph` +
`<name>.data`，`-p map_file_name:=` 与 `ros2 service call slam_toolbox/deserialize_map`
两个入口都能把它读回来，逐像素比对与建图那趟的 `322 x 473` 只差 0.30% / 0.083%
（差值来自解档之后继续进来的新扫描），冷启动对照是 `210 x 449`。
补它的过程里挖出并修掉了**两个兼容层缺陷**：Karto 的 Boost 归档在 UCRT 上按文本模式打开
（读到第一个 `0x1A` 就截断），以及 **`rclcpp::Service` / `rclcpp::Client` 没有
`seek_to_latest()`** —— 同名服务被第二个服务端再建一次时会把通道环里的旧请求重放一遍。
后者不限于 slam_toolbox，任何「重启同名服务器」的路径都会踩。

定位模式（`--mode localization` + `-p map_file_name:=` + 一个初始位姿）2026-09-17 晚也压了一遍
（证据 47）：同一份存档换个进程读回来，`map_start_at_dock:=true` 与 `map_start_pose:=[0.0,0.0,0.0]`
给出**逐位相同**的 `472x473 / occupied=338`，不给初始位姿则是「节点照跑、`/map` 照发、图是空的」
（`occupied=0`）。压它的过程里挖出并修掉了**第三个兼容层缺陷**：`-p` 的值只认标量，
`-p map_start_pose:=[0.0,0.0,0.0]` 会变成字符串、被类型检查退回默认值、而且一声不响 ——
于是节点报「你没有给初始位姿」，而命令行上明明写着。用法见 `02-running.md` 第 6 节末。

但下面这两条仍然是缺口。

| 缺口 | 现状 | 代价 |
| --- | --- | --- |
| ~~**位姿图存档（lifelong / localization 模式）**~~ | **补上了**（证据 41）：`serialize_map` 落盘建图那趟的位姿图（`7 318 952` 字节 `.posegraph` + `385 332` 字节 `.data`），`-p map_file_name:=` 与 `ros2 service call slam_toolbox/deserialize_map` 两个入口都读回来过，逐像素差 0.30% / 0.083%，冷启动对照 `210 x 449`；`ros2 service call` 缺的两个 codec 也补齐了（`bridge_service_roundtrip` 91/91） | 没有了：**跨机**的存档传递仍然没有（写者与读者都在 Windows 这边），见证据 41 末节 |
| ~~**图像格式**~~ | **补上了**（证据 35）：PGM（ASCII `P2` / 二进制 `P5`）之外还有 8 位 PNG，所以 `map_saver` 的 `image_format: png` 能直接写、也能读回来（`mode: scale` 的透明通道因此存得下来）；`bmp` / `jpeg` 仍然没有 coder | 没有了：`slam_toolbox` 的 `map_saver` 默认档（`image_format: png`）不用再改配置 |
| **`merge_maps_kinematic.exe`** | 编出来了；**两个服务 2026-09-17 真调过**（证据 51）：`slam_toolbox/add_submap`（请求里是文件名）与 `slam_toolbox/merge_submaps`，两个 codec 也补进了桥的表（第十七节）。剩下的是**语义**：后者要靠 interactive marker 的反馈来摆放子图，而这套 shim 里没有 rviz 那样的反馈源，零子图时它只是一趟空跑 | 多张子图**合并**这条路没验过。类型通路已经不是障碍了（证据 51） |
| **`map_saver` 的 `system()` 路径** | `slam_toolbox` 自带的 `map_saver.cpp` 靠 `system("ros2 run nav2_map_server map_saver_cli ...")`，Windows 上必失败 | 用 `nav2\ros2_shim\build\map_saver_cli.exe`，没有其他代价 |
| ~~**「原地建图」建不出墙**~~ | **补上了**（证据 65 + 证据 66）—— 先修世界尺度：`run_sim` 的 `--world-size` 现在默认 `12.0`（与 `--range` 相等，墙在半量程处），旧默认 `24.0` 让四面墙全落在 12 m 量程之外（证据 65）。但只改这个**图还是空的**，真根因是 `slam_toolbox` 的三个**运动门槛**（`minimum_travel_distance` / `minimum_travel_heading` / `minimum_time_interval`）出厂是 `0.5 / 0.5 / 0.5`（真车值）：车停着不动，整个跑车过程只提交 **1** 张扫描，一趟 4.5 m 只提交 **9** 张；而 karto 的 `OccupancyGrid::UpdateCell()` 要 `cellPassCnt > min_pass_through`(2) 才写格子，于是只有车周围 ~1.7 m 的一圈格子进图、**占据格子恒为 0**（每面墙的每格只被一张扫描打到，`hitCnt = 1`）。三个值降到 `0.1`（`code/Desktop/racecar/src/racecar/config/slam_toolbox.yaml`；`min_pass_through` 保持上游的 2，实测够用）之后，同一趟走位建出整间 12 m 房：`233x233`、free **82.8%** / unknown 14.9% / occupied **2.4%**（1286 格）；`racecar_ctl.py nav --map slam_ship --goal auto` 用这张 SLAM 图规划并驱动小车成功（`/plan` x8、`/cmd_vel` x77、1.63 m、`SUCCEEDED`） | 只剩与走位相关的部分：unknown 14.9% 是障碍物后面没看见的盲区；窄道、回环（loop closure）、长时间漂移、大图序列化都还没测 |

---

## F. 工程化：这一层目前是零

| 缺口 | 现状【查】 | 后果 |
| --- | --- | --- |
| ~~**版本控制**~~ | **2026-09-17 补上**：`git init -b main` + `git add -A`（**4498 个条目，没有提交** —— 首次提交是「要不要把 220 MB 的 `third_party` 收进历史」的决定，留给仓库所有者）。同时新增 `.gitattributes` 把换行约定写死：`* text=auto eol=lf`、`.bat` / `.ps1` 是 CRLF —— 否则一次 `core.autocrlf=true` 的检出就会让 `tools\check_docs.py` 在没人改过的文件上报错 | 不再是「不能 diff」：`git status` / `git diff` 现在都有答案，回滚只差一次提交 |
| **CI** | `.github` / `.gitlab-ci.yml` / `Jenkinsfile` / `azure-pipelines.yml` 都没有 | 22 + 4 + 52 三套测试全靠人记得跑；没有「提交即回归」 |
| **安装包 / 分发** | Windows 侧运行要带 `bridge\windows\build\` 里的 **87 个 DLL（31.0 MB）**（MSYS2 的 gRPC / protobuf / abseil 只有 DLL，没有 `.a`）【查】；编译要 MSYS2 UCRT64 + Python venv，且按约定只用 6 个大核 | 分发是「复制目录」，没有安装器 / 版本号 / 升级路径 |
| **仓库杂物** | 根目录 `target`（0 字节）、`gRPC\session-ses_f628.md`（361 KB）、`bridge\windows\build\a.exe`（CMake 编译器探测的产物）、`.scratch\` 下一堆调试脚本。根目录那个 128 KB 的 `map_dump.pgm` 已经删掉，但 `map_dump.exe` 的 `--out` 默认还是往当前目录写，手工跑一次就会再出现 | 评审时噪音大；`a.exe` 这类还可能被误当交付物 |
| **文档** | 四篇中文文档 + 两个 README，**没有英文版**，没有 `LICENSE` | 对外协作门槛高 |
| **性能基线** | 只有共享内存那一条吞吐（100 万条 / 608 ms，证据 31 里那次） | 端到端时延（Ubuntu 雷达 → Windows 规划 → 回传）没有量化基线，回归时发现不了性能退化 |
| **故障注入 / 稳定性** | 三套测试里**没有一条**碰断线重连：唯一「杀掉对端」的用例是 `acceptance_check.py` 冷启动时停掉 Windows 侧的守护进程（对端重启、LAN 抖动、丢包都没试过）；也没有长时间 soak 跑【查：`bridge\tests`、`bridge\ubuntu\ros2_bridge_sim\tests`、`gRPC\ros2_gRPC_sdk\tests` 里搜 `restart` / `reconnect` / `kill`，只命中「机器人 toggle 重启」那两条】。2026-09-17 夜复跑验收时碰到一次**冷启动首扫没发现对端**：`topic_list` 5.8 s 后报「远端话题全缺」，而同一轮的 `uplink_nav2` / `cmd_vel_watchdog` / `nav2_demo` / `slam`（都要用 `/odom`、`/cmd_vel`）全过，紧接着重跑 9/9 —— 看着像首扫与对端 agent 的一次竞态，未定位。 | 守护进程里那段「流不是永生的、要重订阅」的逻辑（`bridge_daemon.cpp` 的注释）**没有被验证过**；长时间跑会不会漏内存、会不会把槽位耗光，也没有数据 |

---

## G. 有二进制、但没有证据的清单

这些东西**编译通过了、有的还注册进了插件表**，但一次都没真跑过（或只跑到「创建 + 激活」）：

| 产物 | 状态 |
| --- | --- |
| ~~`vector_object_server.exe`~~ | **起过了**（证据 34）：`ros2 node list` 里有 `/vector_object_server`，`/vo_map`（`nav_msgs/msg/OccupancyGrid`）被发布出来，服务端与管理器都到 ACTIVE 之后干净退出。仍然没有脚本引用它 —— 用的是一份临时生命周期配置（`.scratch\vo_lifecycle.yaml`）【跑】 |
| ~~`nav2_graceful_controller`~~ | **选得到、也跑过了**（证据 34）：`GracefulFollowPath` 现在在 `controller_plugins` 里，同一个目标上的 A/B 两边都 `SUCCEEDED`（见 A 节） |
| `assisted_teleop` / `drive_on_heading` | `behavior_server` 里创建并激活过，没有调用方（证据 21 未验证清单） |
| ~~`navigate_through_poses` 的树~~ | **端到端跑过了**（证据 68）：三航点 `SUCCEEDED` / 47.8 s / 0 次恢复。补了三处 —— `goal_checker_id` 写死成 `general_goal_checker` 让整条目标被动作层拒收、树里缺 `RemovePassedGoals`（航点永不推进，车绕第一个航点转圈）、`rclcpp::copy_all_parameter_values` 是空桩（BT client node 上没有 `robot_base_frame`，每 tick 一条 `frame '' does not exist`） |
| ~~`slam_toolbox` 的 localization / lifelong~~ | **验过了**（证据 41）：`serialize_map` / `deserialize_map` 往返跑通，两条解档路径都有逐像素比对与冷启动对照 |
| `merge_maps_kinematic.exe` | 见 E 节：**服务通路已验**（2026-09-17，证据 51），还剩下「合并语义」没验 |
| `collision_monitor` | 「激光说会撞 → 真的刹停」**做过了**（证据 32：停止多边形放大到 12 m，规划 20 次、控制器一直发 0.45 m/s，车走 0.00 m）；`/collision_monitor_state` 的消息体**已经进编解码表**（2026-09-17，证据 48），`ros2 topic echo` 打得出 `action_type: 2 (slowdown)` + `polygon_name` |
| `route_server` / `docking_server` | 没有图文件 / 没有 dock 实例，只到 ACTIVE |
| MPPI 的 `AckermannMotionModel` / `OmniMotionModel` / 3 个未启用 critic / `visualize` | 用的是上游默认那一套（8 个 critic），一次都没调过（证据 30 未验证清单） |
| SDK 的 TLS 通道 | 开关在，从没走过加密连接 |
| `/parameter_events` | ~~故意不做~~ **已经发了**（证据 39）：构造函数末尾建 publisher（`NodeOptions::start_parameter_event_publisher()`，默认开，`nav2_ros_common::generate_internal_node()` 关掉它）、`declare_parameter` 填 `new_parameters`、应用成功的 `set_parameters` 填 `changed_parameters`、`deleted_parameters` 永远空（shim 没有 `undeclare_parameter`）；ctest `ros2_shim_parameter_events` 盯着负载与「关掉就真的没有」，整条栈起来时图里 14 个 publisher。**跨机这一半 2026-09-17 补上了**：`rcl_interfaces/msg/ParameterEvent` 进了编解码表（证据 48），`ros2 topic echo /parameter_events` 现在打得出 `node:` 与新增/变更的参数；跨机端到端**还没实测**（proto 里仍然没有对应的 RPC，只有话题转发这条路）。本机仍然可以拿自带的 `parameter_events_probe.exe` 用 shim 自己的类型解码打印，作为不依赖编解码表的对照 |
| `bt_smoke.exe` / `tf_probe.exe` / `tf_listener_probe.exe` / `scan_path_probe.exe` / `amcl_probe.exe` / `parameter_events_probe.exe` | 自带的诊断工具，不是回归的一部分（`parameter_events_probe.exe` 见证据 39） |

---

## H. 如果只能再做几件事

按「每单位功夫能取回多少能力」排：

1. ~~**话题 remap**（`-r /a:=/b`）~~ —— **做完了**（证据 32）。`velocity_smoother` /
   `collision_monitor` 现在是 `/cmd_vel` 的必经之路（`nav2_stack.bat --safety`），
   `enable_stamped_cmd_vel` 那套类型/名字的坑也顺手收到了 `nav2_shim_safety.yaml` 一处。
2. ~~**参数服务 + `ros2 param`**~~ —— **做完了**（证据 33）。顺序正是这里写的那样：先服务、后 CLI。
   `parameter_service.hpp` 原来是空壳，现在六个 handler 在 `node.hpp` 里，`ros2 param list / get /
   set / describe / dump` 对着正在跑的 `controller_server` / `bt_navigator` / `amcl` 读写都通，
   而且 `set` 真的会触发节点自己的 on-set 校验回调。跨机改参数（proto 加 RPC）仍然没做。
3. **跨机数组 codec 的命令行入口**（`SmoothPath` / `FollowWaypoints` 的 `poses` / `viapoints`）。
   本机已经跑通（证据 22），跨机只差 `build_goal` 里数组字段的解析。
4. ~~**把 `nav2_graceful_controller` 接进 `controller_plugins`**（一条插件项 + 参数块），
   再把 `vector_object_server.exe` 起一次~~ —— **两件都做完了**（证据 34）。
5. ~~**PNG coder**（只要 8 位灰度，zlib 已经在工具链里）—— 存档就能直接被任何工具打开，
   `slam_toolbox` 的默认配置也不用改~~ —— **做完了**（证据 35）。串行 `ctest` 25/25。
6. ~~**位姿图存档（`serialize_map` / `deserialize_map`）**~~ —— **做完了**（证据 41）。
   `-p map_file_name:=` 与 `ros2 service call slam_toolbox/deserialize_map` 两个入口都读回来过，
   顺带补上 `ros2 service call` 缺的两个 codec，并修掉兼容层里 `rclcpp::Service` / `Client`
   漏掉 `seek_to_latest()` 这个通用缺陷。串行 `ctest` **27/27**（后来补了 `ros2_shim_channel_ring_lifecycle`，见证据 42）。
7. **`nav2_theta_star_planner` / `nav2_constrained_smoother`**（各一个包，形状与已移植的规划器/
   平滑器相同）。
8. ~~**真 ROS2 对端**：找一台装了 ROS2 的机器，把 `ROS2_GRPC_SDK_WITH_ROS2=ON` 编出来
   （`RclcppBackend`），跑一次真实的 7 话题镜像~~ —— **做完了**（2026-09-17 晚，证据 57）：
   Ubuntu 24.04 上装了 ROS2 Jazzy（`bridge\scripts\ubuntu_ros2_setup.sh`），`RclcppBackend` 编出来
   并在跑（`bridge\scripts\ubuntu_ros2_peer.sh`，50111 端口），真图的话题、服务、动作列表都端到端
   量过（`topic echo /rosout` 打出真 talker 的字段；`topic pub /chatter` 在机器人侧收到 21 条；
   `service call /probe_trigger` 回 `success: true`）。顺带撞出两处**只在真对端出现**的 bug
   —— 空 CDR 体被 Fast CDR 拒读、`rmw_serialize` 拿到零 allocator —— 都已修（证据 57）。
   **还差的**：量的是 demo_nodes / `std_srvs` / `action_tutorials` 那几条，**真 nav2 栈上的服务、
   话题与动作还没对着一台真 ROS2 量过**。（`sendActionGoal` 一族原来也挂在这里，
   **2026-09-17 深夜补完了** —— `rclcpp_backend.cpp` 541 → 1235 行，四个 action RPC 落地，见证据 58。）
9. **版本控制 + CI**：`git init` **做完了**（2026-09-17：`git init -b main` + `git add -A`，4498 个条目，见第二十节；**没有提交**，首次提交留给仓库所有者）。**CI 仍然没跑过** —— `.github\workflows\cross-platform.yml` 在等一个远端。

上面 ①②④ 这几条（remap、参数服务 + `ros2 param`、graceful 进表 + `vector_object_server`
起一次、PNG coder）在 **证据 40** 里做过一次现场复检：2026-09-16 全部重跑 —— 串行 `ctest`
25/25、验收 5/5、六个参数服务在真节点上逐个点到、`GracefulFollowPath` 出现在
`controller_server` 的 `Created controller` 列表里并能被 `/controller_selector` 选中后把车开到
目标、`vector_object_server.exe` 起过一次、两个 PNG 互操作检查 0 失败。同一条证据里也写清了
它**不能**证明什么：`Activating controller` 不等于「被选中」。（那时的另一条限制 ——`ctest -j`
并行会随机挂 —— 后来查清了主要根因并大半修掉，见证据 42。）
（那一次记的是当时的 25/25；补上位姿图存档那一条用例之后是 **26/26**（见证据 41）；再补上通道环生命周期那一条是 **27/27**，见证据 42。）

反过来说，**不建议**现在去碰的：RViz 移植（整个 GUI 栈，收益只是「看得见」）、
rclpy 全量绑定（要重做一遍 Python 生态）、Gazebo/物理仿真（本项目的仿真已经够验证链路）、
`nav2_system_tests` 移植（依赖 rclpy + 仿真器，性价比最低）。

## 2026-09-17 复核：把「能用什么 / 还缺什么」重新量了一遍

本节是 2026-09-17 的一次完整复核，命令都写在表里，可以照着重跑。凡与上文冲突的以本节为准，
冲突处用 **修正** 标出。

### 一、这轮实跑过的（不是复述上文）

| 套件 | 命令 | 结果 |
| --- | --- | --- |
| 兼容层 + 魔改 nav2 | `.scratch\run_ctest.py 1`（`nav2\ros2_shim\build`，6 大核） | **27/27、53.88 s** |
| 桥接层 | `ctest --test-dir bridge\windows\build` | **4/4、2.64 s** |
| gRPC SDK | `ctest --test-dir gRPC\ros2_gRPC_sdk\build` | **4/4、1.75 s** |
| 共享内存通道 | `ctest --test-dir nav2\shm_transport\build` | **1/1、0.97 s** |
| Ubuntu 仿真（在 Windows 上跑） | `pytest ros2_bridge_sim\tests -q -rs` | **61 passed、21.2 s、0 skipped**（52 → 61：证据 49 的 `test_watch.py`） |
| 五项检查（当时脚本里只有 5 条） | `python bridge\scripts\acceptance_check.py` | **5/5** |
| 第六条检查 `check_slam`（2026-09-17 晚补） | `python bridge\scripts\acceptance_check.py` | **6/6、98 s**（单跑 62.8 s、`occupied=401`；证据 45） |
| 整栈，AMCL 版 | `nav2_stack.bat --goal auto` | **SUCCEEDED**：7.20 s、2.376 m、plan 18 点、feedback 329、recovery 0 |
| 整栈，SLAM 版 | 见第五节 | **SUCCEEDED**：6.09 s、1.129 m、plan 10 点、feedback 316、recovery 0 |
| 34 条 ctest 逐条直跑 | `.scratch\_audit_ns.py`（进 `ns_assertions.json`） | **34/34、rc 全 0、450 行断言 pass / 0 fail** |
| `ctest -j 8` × 10 轮 | `.scratch\ctest_stress.py --rounds 10 --jobs 8` | **7/10 轮干净**，3 轮不干净（见第七节）；复修后重跑 **10/10**（见第九节） |

四个构建树一共 **36 条 `ctest` 注册、34 条不同名**（`shm_transport` 被注册了 3 次：
`nav2\ros2_shim\build`、`bridge\windows\build` 的 `add_subdirectory`、独立的 `nav2\shm_transport\build`）。

### 二、四处修正：测试清单本身

1. **`bridge_shm_roundtrip` 从来不会注册。** `bridge\windows\CMakeLists.txt:230` 写的是
   `if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/shm_roundtrip.cpp)`，而 `bridge\windows\tests\`
   下只有 `cdr_check.cpp`、`service_roundtrip.cpp`、`action_roundtrip.cpp`。所以 `bridge\windows\build`
   的 4 条是「3 条桥接 + `add_subdirectory(nav2/shm_transport)` 带进来的 `shm_transport`」。
   **修正**：不是「多了一条重复且源文件缺失的测试」，而是那段 CMake 是死分支，第 4 条是重复的 `shm_transport`。
   **2026-09-18 已补上**：`bridge\windows\tests\shm_roundtrip.cpp` 写出来了 —— `ros2_bridge/shm_topic.hpp`
   的名字（含 `NAV2_SHM_NAMESPACE` 前缀）、每个名字拿到的档位大小、两把手柄之间的 publish/drain
   往返（逐字节、保序、不丢），以及整槽与超槽两个边界，52 处断言。那棵树的 `add_test` 因此是 5 条、
   `ctest` 报 **6/6**（第 6 条是 `shm_transport`）。假了一次照样红：把往返的期望值 +1，
   `1 of 52 checks failed`、rc=1。
2. **`gRPC\ros2_gRPC_sdk` 的那 4 条从没进过文档。** 在 `02-running.md` / `03-validation.md` /
   `04-gaps.md` 里搜 `localhost_e2e`、`services_localhost`、`discovery_localhost`、`config_localhost`
   都是 0 次；文档里的「全部 ctest」实际只覆盖 27 + 4 = 31 条。
3. **`test_nav2_ros_common` 编了但从不跑。** `nav2\ros2_shim\CMakeLists.txt` 里 `add_executable` 38 个、
   `add_test` 26 条，它是只有前者的那一个（编译期验证，ctest 的 27 条里没有它）。
   另外 `bridge\tests\cdr_check.cpp` 里一个 `CHECK` 宏都没有，靠 `expectEqual()` 累加
   17 个 fixture / 51 组字节比较 —— 不是缺陷，但静态看「断言密度」会把它当成空测试。
4. **隔离是「opt-in」的，只有 `ctest` 会 opt in —— 直跑 exe 就等于和活着的守护进程共用一份图。**
   `ctest` 给每条测试注入 `NAV2_SHM_NAMESPACE=ctest_<名字>`（各 `CMakeLists.txt` 的
   `ENVIRONMENT_MODIFICATION`），而 `graph.hpp:143` 的 `graph_dir()` 确实会把同一个命名空间
   追加到图目录上（`<temp>/nav2_shim_graph_<NAMESPACE>`），所以**测试之间**是隔离的 ——
   这一层没问题。问题在没人设的时候：默认命名空间是空串，于是**凡是绕开 `ctest` 去跑同一个
   exe 的人**（手工跑、自己写的脚本、将来换一个 runner）都会静默地和守护进程、和彼此共用图。
   A/B 实测（`.scratch\_audit_ns.py`，逐条日志 `.scratch\ns_<名字>.log`）：
   - **不加**命名空间直跑全部 34 条：`slam_toolbox_serialization` rc=1，5 条 FAIL
     （`diff.same_shape`、`before_cells.occupied == after_cells.occupied` …），同屏还有满屏
     `Failed to compute odom pose`；
   - 每条都补上 `NAV2_SHM_NAMESPACE=ctest_<名字>` 后重跑：**34/34 全绿、rc 全 0、450 行断言**。
   也就是说：`02-running.md:947` 那句「图注册表也跟着那个命名空间走」是对的，但它只在
   `ctest` 这条路下成立。
   **2026-09-17 晚更正：这里原来写的「要改的是默认值（默认按 pid 或按可执行文件派生命名空间）」
   是错的，而且照着改会把整个项目拆掉。** 实测（同机、守护进程活着）：默认命名空间下
   `ros2 topic list` 列出 **7 条**话题；只把 `NAV2_SHM_NAMESPACE` 换成一个每进程唯一的值，
   同一条命令**一条都列不出来**。原因很直白 —— `graph_dir()`（`graph.hpp:143`）和
   `channel_name()`（`include/rclcpp/shm_config.hpp:143`）读的是同一个变量，而
   「跨进程共享一份图」正是这个 shim 的存在理由：守护进程、`ros2.exe`、nav2 节点靠的就是
   它。**默认值必须保持共享。**
   **所以正确的修法是让每个「测试」自己隔离，而不是动产品默认值。** 这一条现在四处构建树都做到了
   （shim 26 条靠目录级循环；`shm_transport` 1 条自己直接设；`bridge\windows` 3 条是这一轮补的，
   见下面第 8 条；`gRPC\ros2_gRPC_sdk` 那 4 条一个 shim 头都不包含，不需要）。
   具体的那个漏网之鱼是 `shm_transport`：`nav2\shm_transport\CMakeLists.txt:47` 是当时**唯一**
   一条没有 `ENVIRONMENT_MODIFICATION` 的 `add_test`，它跑在默认命名空间里，
   通道和图目录都跟活着的守护进程共用一份 —— 而它正好是唯一会挂死的那条测试（第七节第 3 条）。

5. **`gRPC\ros2_gRPC_sdk` 那 4 条不是「0 断言」，上一轮记错了。** 上一轮静态扫描只按大写
   `CHECK` / `EXPECT` / `ASSERT` 计数，而这棵树用了**两套**断言命名：大写宏一套，
   小写 helper 函数（`check()` / `expect()`）一套。gRPC 那 4 条全是小写的
   `check(bool, const std::string&)`，命中数 `config_localhost` 29、
   `discovery_localhost` 19、`localhost_e2e` 32、`services_localhost` 47 —— 共 **127 处**；
   `check()` 走 `[FAIL]` 分支时 `++g_failures`，`main` 最后 `return g_failures == 0 ? 0 : 1`，
   所以**它们能失败，是真测试**。同一口径的误判还波及 15 条 shim 测试（`navfn_planning`、
   `params_file`、`channel_ring_lifecycle`、`tf_buffer_lookup`、`tf_time`、`tf_convert`、`graph_rewrite`、`clock`、
   `uuid`、`remap`、`parameter_service`、`parameter_events`、`parameter_event_handler`、`plugin_names`、`map_io`、
   `ompl_spaces`），它们也都只有小写计数。
6. **`ros2_shim_compile` 和 `ros2_shim_compile_check` 是两条不同的测试**，上一轮把它们并成一条记了。
   前者是 `test_shim_compile` ← `tests\test_shim_compile.cpp`（**1 635 字节**，真跑
   `pluginlib::ClassLoader` 加载 + `LifecycleNode` + `create_publisher`，加载失败或 `value() != 42` 就 `return 1`）；
   后者是 `compile_check_messages` ← `tests\compile_all_messages.cpp`（**209 字节**，只把每一个
   生成的消息头 `#include` 一遍，0 断言 —— 这才是「纯编译」那一条）。
7. **验收 4（SLAM）不在任何自动化验收里 —— 这条已经补上了。** 发现的时候 `bridge\scripts\acceptance_check.py`
   （当时 14 360 字节）只覆盖验收 1（`check_topic_list` / `check_topic_info` / `check_scan_rate`）、
   验收 2（`check_uplink`）、验收 3（`check_nav2_demo`），整个文件 **grep `slam` 零命中** —— SLAM 那条
   只有 `.scratch\_audit_slam_nav2d.py` 这种手跑脚本，换一台机器、换一个人就没有入口。
   **2026-09-17 晚已加第 4 条检查** `check_slam`（脚本里现在是 6 条检查，新增 `--skip-slam` 开关），
   实跑 65.1 s、`occupied=384`；细节和这一次发现的其它问题见证据 45。
8. **`bridge\windows` 那 4 条一条都没隔离 —— 其中 2 条在往活着的栈里写记录。** 上面第 4 条当时
   只点了 `shm_transport`，说它是「整棵树里唯一一条没有 `ENVIRONMENT_MODIFICATION` 的
   `add_test`」；这句话按 `add_test` 数是对的，**按「用了 shim 的测试」数就不对**：
   `bridge\windows\CMakeLists.txt` 里 4 条 `add_test`（实注册 3 条，`shm_roundtrip.cpp` 不存在，
   那段是死分支）一条 `ENVIRONMENT_MODIFICATION` 都没有，而 `bridge_service_roundtrip.cpp`
   和 `bridge_action_roundtrip.cpp` 都 `#include "ros2_shim/graph.hpp"` 并用 `ShmChannel`，
   `action_roundtrip.cpp` 里还有三处直接查图（`:905` `action_exists(name)`、
   `:907` `action_type(name) == "ros2_bridge_sim/action/Drive"`、`:1059` `!action_exists(name)`）。
   A/B 实测（`.scratch\_ns_pollute_ab.py`；守护进程活着，同一个 exe 各跑 12 次，跑的同时用
   `ros2 node list` / `action list` / `service list` 轮询）：
   - **不设命名空间**：跑的时候活着的图里出现 `/action_roundtrip`（节点）、`/test/drive`（动作）、
     以及 `/action_roundtrip/` 下面 `describe_parameters` / `get_parameter_types` /
     `get_parameters` / `list_parameters` / `set_parameters` / `set_parameters_atomically`
     六个参数服务；12 次 rc 全是 0。
   - 设 `NAV2_SHM_NAMESPACE=probe_iso`：**一条都没有**，12 次 rc 全是 0。
   也就是说这不是「测试偶然失败」，而是**测试读写的正是产品的图**：`ros2 node list` 会多出一个
   不属于任何栈的节点，`ros2 action list` 会多出一个 `/test/drive`。而 `bridge_daemon.cpp:1306`
   是按「来源不是 `remote:*` 才算本地」来分本地 / 远端的（`:1356` 自己也是用
   `"remote:" + peer_` 这个拼法写远端记录），测试写的记录来源正好是 `remote:test` ——
   **形状上和真对端的广告一模一样，栈里没有字段能把它区分出来。**
   **已修**：`bridge\windows\CMakeLists.txt` 末尾加了和 shim 树同款的目录级循环
   （`get_property(... PROPERTY TESTS)` + `set_tests_properties(... ENVIRONMENT_MODIFICATION)`），
   守护进程活着时跑 `ctest --repeat until-fail:8`（32 次）验证：**4/4 全过、rc=0、
   轮询到的污染 0 条**（`.scratch\_bridge_pollute_after.py`）。
   仍然成立的一条：**手工直跑某个测试 exe 还是共用默认命名空间** —— 这是产品语义决定的
   （见第 4 条的更正），所以「跑测试」的正确入口是 `ctest`，不是直接跑 exe。

**断言密度（2026-09-17 重数，两套口径都算）**：26 条 shim + 4 条 gRPC + 3 条桥接 + 1 条 `shm_transport`
= 34 条测试，**912 处断言调用点**（大写宏 217 + 小写 helper 695），逐条清单和计数在
`.scratch\_assert_census.json`。真正「一个断言调用都没有」的只有 **2 条，而且都是故意的**：
`compile_all_messages.cpp`（209 字节纯编译）和 `test_shim_compile.cpp`（靠 `return 1` 判失败，
不走 helper）。实跑打出 **450 行 `pass`**。三类「静态看像空测试」的要分清：上面那两条纯编译、
`bridge\tests\cdr_check.cpp` 一个 `CHECK` 宏都没有但实跑 17 行（17 个 fixture × 3 组字节比较，
靠 `expectEqual()` 累加）、`ros2_shim_messages` 不打印 `pass` 行而是打印 `[test]` 段落（里面 54 处 `CHECK`）。
打印最多的是 `bridge_action_roundtrip` 147 行、`bridge_service_roundtrip` 91 行。
另有 **21 个 exe 有 `add_test` 之外的用途**（服务端/工具/探针），「名叫 test 却从不执行」的
仍然只有 `test_nav2_ros_common`。

顺带核掉一条误判：`test_cdr_matches_rosbags.py` 的 SKIP 是真的。在 Ubuntu 上直接跑，实测输出
`SKIP: rosbags is not installed (No module named 'rosbags')`、`exit=0`，与文档口径一致，不是缺口。

### 三、对端不是 ROS2，而且连测试都跑不起来

`~/code/CPU/.venv` 的 `pip list` 全文：`grpcio 1.83.1`、`numpy 2.5.3`、`pip 26.2.1`、
`protobuf 7.36.1`、`typing_extensions 4.16.0`。`pytest`、`rosbags`、`rclpy` 都没有，
`which ros2` 为空，`requirements.txt` 自己也写着「no rclpy, no ROS2 installation」。所以：

- 那 61 条 pytest **只能在 Windows 上跑**（本轮就是这么跑的），在机器人上连收集都起不来。
- `test_cdr_matches_rosbags.py` 要在仓库里找 `.msg`，而 `~/code/CPU` 下只有 `bridge` 一个目录。
- 文档里 `RclcppBackend` 那一半，**从来没有在真的 ROS2 上编译过**。

### 四、跨机边界：在活着的全栈上量出来的

- `ros2 interface list`：2026-09-17 白天 **38 msg / 38 srv / 9 action = 85 种**能过机器（话题
  从 19 加到 38，见证据 48）；**当晚 96**（action 9 → 20，证据 61）、**再往后 98**
  （`Image` + `CameraInfo`，证据 62）。所以这张表最后是 **40 msg + 38 srv + 20 action**。
  生成头文件那一侧是 **216 msg / 83 srv / 20 action = 319 种**：按收工时的表，
  msg 过 **40/216 = 18.5%**、srv 过 **38/83 = 45.8%**、action 过 **20/20**。
- 整栈起来后 `ros2 topic list -t`：**73 条话题、32 种类型，0 条对不上**（2026-09-17 复测，
  证据 48）。加表之前这一句是「67 条话题，其中 27 条的类型不在表里」（nav2_msgs 13、
  map_msgs 5、geometry_msgs 4，nav_msgs / dwb_msgs / rcl_interfaces / std_msgs /
  visualization_msgs 各 1 —— `/controller_server/critics_stats`、
  `/global_costmap/costmap_updates`、`/particle_cloud`、`/speed_limit`、`/evaluation`、
  `/parameter_events` 都在这 27 条里）。同一轮里 32 种类型**全部**打得出字段（0 条退化成
  `(N bytes of ...)`），另有 41 条话题在 6 s 窗口里没有消息（selector、`*_updates`、
  `/evaluation` 这类只在特定情况下才有流量）。
- nav2 的 20 个 srv 只认 3 个（`Toggle` / `SetInitialPose` / `ManageLifecycleNodes`）——
  **2026-09-17 补到 12 个**（九条 costmap / planner / map_server 的 srv，见第十八节），
  **同日再补到 20/20 全认**（collision_monitor / object server / route / docking 那八条，见第十九节），
  19 个 action 当时只认 8 个（外加仿真自己的 `ros2_bridge_sim/action/Drive`）——
  **2026-09-17 深夜补到 20/20 全认**（第二十三节 / 证据 61）。
  `slam_toolbox` 的 11 个 srv **2026-09-17 补全了**（第十七节）。
  上一版拿 `ros2 service call /global_costmap/clear_entirely_global_costmap nav_msgs/srv/ClearEntireCostmap`
  → `no codec for service type ...` 当例子 —— 这一条**现在能调通了**，所以要换一个仍然
  `no codec` 的：`ros2 service call /collision_monitor/add_exclusion_zone
  nav2_msgs/srv/AddExclusionZone`（`nav2_msgs` 那 20 个 srv 里剩下 8 个之一）。
- ~~**上行默认只有 3 个名字**~~ —— **2026-09-17 关上了**：默认表现在是 `/cmd_vel` /
  `/goal_pose` / `/initialpose` **加上 nav2 回话的那两个** `/plan` / `/behavior_tree_log`
  （`bridge_daemon.cpp` 的 `Options::uplink`）。`ros2.bat` / `ros2_bridge.bat` 一行没动，
  因为默认值就长在守护进程里。选这两条而不是两张 costmap 的理由是「事件驱动 vs 定时」：
  一条路径算出来、一次行为树分支走完才发一条，闲置时一分钱不花；costmap 是定时重发几百 KB，
  要就自己 `--uplink /global_costmap/costmap`。名字仍然只是名字 —— 哪一端都学不到类型时它
  静默跳过（`startUplinks()` 每 `--refresh-ms` 重试一次），所以多列两个名字没有代价。

结论：验收里「Windows 发话题、Ubuntu 订阅」这条，`/cmd_vel` 与 nav2 的 `/plan`、
`/behavior_tree_log` 都跑过了（证据 54：守护进程 `uplink=5`，`/plan msgs=16`、
`/behavior_tree_log msgs=15`，全部 `failed=0`；机器人侧把 `/plan` 逐字段解出来）。
`/goal_pose` 与 `/initialpose` 在没有 nav2 节点时是 0 条 —— 它们要等 `bt_navigator` 起来
（`self_client_` 订阅 `/goal_pose`）才注册得上，这是「名字先有类型才有通道」的正常表现。

> **2026-09-17 更新（证据 49 → 54）**：这件事分两步走完。证据 49 解决「**怎么**把名字写进
> 命令行」（`--uplink` 可重复；逗号列表会被 `cmd.exe` 吃掉），并证明 4 个话题、28 条消息能在
> 机器人侧逐字段解出来；证据 54 解决「**开箱即用**」——同一条链路现在不写任何参数：
> `ros2_bridge.bat start` 之后起 nav2，守护进程自己把 `/plan` 与 `/behavior_tree_log` 转过去。
> 仍然没做的是**成本控制**：上行没有 `--throttle-ms` 那样的节流，也没有正则选择（`--mirror` 有）。

> 守护进程 `err.log` 里躺着一条真实告警：某一轮 `/cmd_vel` 的本地写者用了 `TwistStamped`，
> 而机器人要 `Twist`。这正是 `nav2_shim_overrides.yaml` 把 `behavior_server` / `controller_server`
> 的 `enable_stamped_cmd_vel` 都钉成 `false` 的原因，也说明「同名不同型」这条边界是活的、会复发。

### 五、SLAM：能用，但顺序不能反

**能做出来。** 2026-09-17 01:23 的一轮（`.scratch\_audit_slam_nav2d.py`，输出
`.scratch\audit_slam_nav2d.out`）：

1. `ros2_bridge.bat stop` → `ros2_bridge.bat start --mirror /clock --mirror /odom --mirror /scan --mirror /tf --mirror /tf_static`
   —— **故意不镜像 `/map`**，让 `slam_toolbox` 当唯一地图源；
2. `service call /sim/reset` 把车放回原点；
3. `slam_demo.bat -p base_frame:=base_link -p max_laser_range:=12.0 -p map_update_interval:=2.0 --duration 600`；
4. 车走一圈：4 ×（直行 2 m + 转 90°）；
5. `map_dump.exe --topic /map` → `473x473 res=0.0500 occupied=421 free=41294 unknown=182014`；
6. `nav2_stack.bat --no-amcl --goal "1.0,-1.0"` —— `--no-amcl` 是因为 `map -> odom` 这一段由 SLAM 提供；
7. 结果：`SUCCEEDED`，6.09 s、`travelled 1.129 m`、距目标 0.159 m、feedback 316、recovery 0、
   `/cmd_vel` 118 条（117 条在动，峰值 0.45 m/s）、`/plan` 6 条（10 点）、ack 延迟 42.8 ms；
8. 跑完再看同一张图：`occupied=523 free=50538` —— **边导航边建图，图在长**。

**但第一轮是失败的，值得记下来。** 同一套流程，如果**先起 nav2、车还没动**就去 `--goal auto`：
此时 SLAM 的图几乎是空的（`occupied=0 free=572`），`nav2_goal` 只能回
`no direction out of the robot is clear for 0.5 m; is /map the map the robot is actually in?`，
1.73 s 就退出。原因不是 SLAM 坏了，而是 `slam_toolbox` 要等车动了才把新扫描插进图里，
而 `--goal auto` 的目标是从图上挑的 —— 鸡生蛋。所以顺序必须是「先让车跑一圈把图建出来，
再让 nav2 接管」，或者给 `--goal X,Y` 一个明确的点。

**缺的是周边，不是核心**（与 E 节一致，这里补数字）：`slam_toolbox_port` 编的是
`async_slam_toolbox_node` / `sync_slam_toolbox_node` / `localization_slam_toolbox_node` /
`merge_maps_kinematic` / `slam_bridge_node`；明确排除 `solvers/ceres_*`（Ceres 在 MSYS2 只有 DLL，
用自写 Eigen 解算器顶 `solver_plugins::CeresSolver` 这个名字 —— 节点日志里打的就是这一行）、
`rviz_plugin/`、`src/experimental/`（lifelong mapping 要 interactive markers）。
**验收脚本里原本没有 SLAM 一项**（`acceptance_check.py` 只有 topic_list / topic_info /
scan_rate / uplink / nav2_demo 五条）；**2026-09-17 晚已补上第六条 `check_slam`**：
它自己停掉守护进程、以「不带 `--mirror /map`」重启、起 `slam_demo.bat`、开一圈闭环、用
`map_dump.exe` 读回占用格数，收尾再把默认守护进程拉回来（证据 45）。**当晚这条检查又长出
第二阶段**（证据 47）：同一圈跑完再 `serialize_map` 落盘、杀掉这个进程、用
`--mode localization -p map_file_name:=<存档> -p map_start_at_dock:=true` 起第二个进程，
读回来的图必须还是那一张 —— 「建图 → 存图 → 换进程定位」这条闭环现在是自动跑的一条。
代价是 `check_slam` 62.8 s → 82.8 s。SLAM 本来还有 4 条单元测试（`slam_toolbox_mapping` 等）。

### 六、顺手撞到的四个坑

1. **`nav2_stack.bat --goal X,Y` 里的逗号会被 cmd 吃掉。** 实测（`.scratch\_echoargs.bat`）：
   `--goal 1.0,-1.0` → `%1=--goal`、`%2=1.0`、`%3=-1.0`，`nav2_goal` 于是报
   `--goal wants X,Y or auto, got '1.0'`；写成 `--goal "1.0,-1.0"` 才对。
   脚本头部注释和 `:usage` 里的例子 `nav2_stack.bat --goal 3.5,-2` 因此是错的，要加引号。
2. **无控制台的调用者等不到 `nav2_stack.bat` 返回。** 本轮实测：目标 01:24:41 就跑完并写了 JSON，
   但 `cmd /c nav2_stack.bat --goal auto ...` 这个调用进程一直活到 400 s 超时（8 个节点还活着，
   `cmd` 就不撒手）。脚本要继续只能轮询 JSON，不能等退出码。
3. **`slam_demo.bat` 没有 `stop`。** 杀掉 `cmd /c slam_demo.bat` 的壳不会杀掉
   `slam_bridge_node.exe`：本轮实测壳死了、节点还在继续发 `/map`，害得下一轮「等 `/map` 消失」
   的循环空转 150 s。清理必须补一句 `taskkill /F /IM slam_bridge_node.exe`。
   （好消息：节点一死，它在图注册表里的条目立刻消失，没有残留。）
4. **守护进程日志没有上限。** `bridge\windows\build\ros2_bridge_daemon.log` 现在 **51 MB / 49.8 万行**，
   每 5 s 一版状态表，没有轮转。

### 七、`ctest -j 8` 十轮压测：三种失败形状，都不是「随机」

`.scratch\ctest_stress.py --rounds 10 --jobs 8`（同样钉 6 大核；失败一轮留一份
`.scratch\stress_fail_<轮>.log`，逐轮汇总在 `.scratch\ctest_stress.json`）：

| 轮 | 结果 | 失败者 |
| --- | --- | --- |
| 1 | 20.8 s、96%（27 条里挂 1） | `ros2_shim_graph_rewrite` |
| 2 / 4 / 5 / 6 / 7 / 8 / 10 | 14.7 ~ 22.1 s、100% | — |
| 3 | 19.9 s、96% | `slam_toolbox_mapping` |
| 9 | **900.0 s、超时被杀** | `shm_transport`（挂住） |

**7/10 轮干净。** 三种形状，三个不同的成因：

1. **`ros2_shim_graph_rewrite`：读者的耐心是墙钟，会先于写者放弃。**
   `nav2\ros2_shim\include\ros2_shim\graph.hpp:230` 的 `read_shared()` 用 `CreateFileW` +
   `FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE` 重试，`retry_swap`（`:204`）给它的预算是
   **60 ms**；写者 `install_file()`（`:295`）先 `MoveFileExW` 再 `ReplaceFileW`，预算 **50 ms**。
   两边都是有限预算：机器一超订（8 个 job 挤 6 个核），写者可能正在 `ReplaceFileW` 中途被换下，
   读者 60 ms 用完就报「打不开」。实测形状是第一轮那句
   `1295 reads, 1 missing or half written (1 not openable, 0 empty, 0 without #end) | 1x ERROR_SHARING_VIOLATION`。
   它对调用方的后果正是 `graph.hpp:223` 注释里写的那个 —— 一个正在服务的节点被读成「什么都没提供」。
   这一条不是新形状：`02-running.md:958` 和证据 42 里已经记着「26 轮 / 11 354 次读里 13 次打不开」。
   **修的方向**是让读者这一侧没有墙钟上限（或让 `subscription_count` 这类门控退化成
   「不确定就当作有人在听」），而不是把 60 ms 调大一点。

   **2026-09-17 已修**（证据 43）：读者的预算改成**跟着错误码走** —— `ERROR_FILE_NOT_FOUND` /
   `ERROR_PATH_NOT_FOUND`（名字不在：要么是交换在飞，要么是记录真的没了）保持 60 ms，
   其余（`ERROR_SHARING_VIOLATION` / `ERROR_ACCESS_DENIED`：名字在、只是有人按着）给 **1000 ms**。
   仍然是上界，所以「读不会挂住」这条性质没丢。同一台机器、同样钉 6 大核的 A/B（`.scratch\_patience_ab.py`）：
   改之前 20 轮里 **2 轮**各丢 1 个读数（`1 678 reads` / `1 652 reads` 各 1 次 `not openable`），
   改之后 **20 轮 0 轮**。
2. **`slam_toolbox_mapping`：一次被拒绝的图文件交换，会把那条注册永久丢掉 —— 而失败是静默的。**
   这个测试**是**带命名空间的（`CTestTestfile.cmake:53` 里 `NAV2_SHM_NAMESPACE=set:ctest_slam_toolbox_mapping`），
   所以它的图目录是自己的，跟守护进程和其他测试无关；上一版这里写的「250 ms 计数缓存」是**错的**：
   `test_slam_mapping.cpp:177` 是该进程的**第一次**图查询，而 `merged_topics_cached()`
   （`graph.hpp:899`）的 `static` 缓存第一次调用必然刷新，不可能给出 0。真正的链条是：

   - 合并视图**只读文件**。`collect()`（`graph.hpp:789`）遍历图目录里的 `*.graph`，
     从不回看本进程内存里的 `ProcessRegistry`；`subscription_count()` / `publisher_count()`
     （`graph.hpp:930` / `:943`）问的就是它。
   - 每次注册都会**整份重写**自己的文件，写法是把 `.tmp` 换过去（`flush_locked()` → `install_file()`，
     `graph.hpp:723` / `:295`）。`MoveFileExW` / `ReplaceFileW` 会被任何**不共享 delete** 的持有者拒绝
     —— 索引器、杀软，或者一个用 `std::ifstream` 读图的旧程序。
   - **而 `flush_locked()` 把 `install_file()` 的返回值丢掉了**（`graph.hpp:723`），
     并且 `ros2_shim/graph.hpp` 里**没有任何后台线程**会把脏数据重新刷一次。也就是说：
     一次被拒绝的写入 = 那条注册**永久**消失，直到这个进程下一次图变化。
   - 后果就是 `Publisher::get_subscription_count()`（`include\rclcpp\publisher.hpp:75`）对一个
     真实存在的订阅回答 0，nav2 / costmap / slam_toolbox 的「没人在听就不发布」门控于是静默丢消息。

   **这条已经用两个可随时重跑的确定性实验证明**（不是推测）：

   | 实验 | 做法 | 结果 |
   | --- | --- | --- |
   | `.scratch\_greedy_graph_hold.py` | 从 t=0.037 s 起把 `<pid>.graph` 按下不共享 delete 打开 6 s（头几次写入全丢） | rc=1：`:177` 挂，**并且 `map_messages > 0` 等 5 条也挂，`0 map messages`** |
   | `.scratch\_partial_graph_hold.py` | 先让前几次写入落地，等文件里已经有 `/map` 和 `pose`，再从 t=0.185 s 起按住 2.0 s（只丢 `activate()` 那一次） | rc=1：**只有 `:177` 挂**，`45/45` 位姿、`239 map messages`、`279 occupied` 全部照常 |

   第二个就是第 3 轮的**逐个字段一致的签名**（第 3 轮：只有 `:177` 挂、45 位姿、237 条 map、279 occupied）。
   第一个则说明这条缺陷的**上限比计数为 0 严重得多**：同一个成因能让 SLAM 一条地图都不发。
   门控是「不问就当作没人听」，而图是「丢了不报错」—— 两者叠起来就是「车不动 / 没有地图，且日志里什么都没有」。

   **修的方向**（三选一或叠加）：`collect()` 把本进程内存里的记录并进来（本进程的记录永远准）；
   或者 `flush_locked()` 记下失败并在下一次查询/心跳时重刷；或者让门控在拿不准时按「有人在听」处理。

   **2026-09-17：改掉了一半 —— 本进程对自己的记录不再依赖那次写入落地。** `collect()` 走到本进程
   自己那份 `<pid>.graph` 时不再读文件，改成直接从内存里的注册表取（`ProcessRegistry::merge_own()`）：
   文件本来就只是「写给别人看的那份拷贝」，而拷贝的好坏取决于最后一次改写有没有被接受。于是
   「一个正在跑的节点被读成什么都没提供」这个后果，对**本进程自己**的查询不再成立 —— 而 nav2 的
   发布门控问的正是本进程里的那个发布者。

   **判定标准是那两个复现脚本，不是推理**（都跑过）：`.scratch\_partial_graph_hold.py`
   （从 t≈0.19 s 起按住自己的图文件 2.0 s）**从 rc=1、`:177` 挂 → rc=0、`all checks passed`**；
   `.scratch\_greedy_graph_hold.py`（从 t=0.037 s 起按住 6.0 s，原来还会让 SLAM 一条地图都不发）
   **从 rc=1、`0 map messages` → rc=0、45/45 位姿、239 条 map、279 occupied**，与它自己的对照组
   逐字段相同。全套 `ctest` 仍然是 27/27。

   **没改掉的那一半，是跨进程的**：文件照旧要写、别的进程照旧读文件，所以「写的时候被按住 →
   别人的视图里少了这条注册、或者晚一代」仍然可能发生；`graph.hpp` 里也仍然**没有任何线程**
   去重刷。动手前先看这个坑：`ReplaceFileW` 失败时**替换文件（`<pid>.graph.tmp`）可能已经被它
   带走**，所以「原地把同一个 `install_file()` 再调一次」并不成立 —— 要么按「重写 `.tmp` → 再换」
   的顺序重试，要么起一个后台重刷线程。

3. **`shm_transport`：整条测试挂死，不自己回来；而且它是唯一没有做隔离的那条。**
   `nav2\shm_transport\CMakeLists.txt:47` 是整棵树里**唯一**没有 `ENVIRONMENT_MODIFICATION` 的
   `add_test`。**2026-09-17 晚更正：这句话当时就不准** —— `bridge\windows\CMakeLists.txt` 当时
   也没有（4 条 `add_test`、0 条隔离），准确说法是「shim 这棵树里唯一」；已经补上，见第二节第 8 条。
   所以它的**图注册表目录**跟活着的 `ros2_bridge_daemon` 是同一份（第 4 条讲
   「隔离是 opt-in 的」，这就是那个没 opt in 的实例）。要留意：给它补上命名空间**也救不了它的
   通道** —— 这个测试直接用 `ShmChannel::create(name, ...)`，而 `NAV2_SHM_NAMESPACE` 当时只作用在
   shim 由话题名派生的通道名上，它的通道名是写死的（见下面「挂在哪」；**这一处后来改掉了**，见本节末）。
   挂死本身是三种里最严重的：不给调用者任何东西，只是不回来。进程表实测：第 9 轮 01:37:12 起的 `test_shm.exe`（pid 21548）到 01:50:41 还活着，
   **813 s 墙钟、1.11 s CPU**，也就是一直阻塞而不是在算；ctest 的输出停在第 24 条的半行上
   （`24/27 Test #24: slam_toolbox_mapping ...................   Pas`），然后被脚本的 900 s 超时截断，
   见 `.scratch\stress_fail_009.log`。随后一轮（第 10 轮）14.7 s 干净跑完，所以不是环境坏了。
   `02-running.md:963` 里「`shm_transport` 挂住 259 s 后被 kill」说的就是它，这次挂到 900 s 超时。

   **单跑也能挂，而且不止一次。** 把它单独拎出来（自己的命名空间、同样钉 6 大核，
   `.scratch\_shm_transport_where.py`、`_shm_transport_under_load.py`、`_shm_transport_locate.py`）：
   约 **131 次里挂 2 次（≈1.5%）**，两次都是整进程不回来 ——

   - 隔离命名空间 + 3 个压核线程，第 3 次就挂，**200 s 上限被杀**。200 s 远超测试自己那个
     `WaitForSingleObject(..., 40000)`（`test_shm.cpp:378-379`），所以是**真的死等**，不是「慢」；
   - 隔离命名空间、30 s 上限，第 6 次挂。

   剩下 129 次全绿（包括一次 40/40、一次 25/25）—— 低频，不能指望随手抓到；但**它不依赖守护进程**，
   隔离模式下照样挂。

   **挂在哪：`publish()` 里那次没有超时的等待 —— 已用 hold/release 对照证明。**
   把 `test_shm.cpp` 里每一次 `subscribe()` 列出来会发现超时**全是正数**
   （`0` / `200` / `500` / `1000` / `5000`，见 `:114 :181 :205 :214 :240 :319`），所以
   `subscribe` 里那个 `WaitForSingleObject(impl.event, wait)` 在这套测试里**永远有界**。
   整套测试唯一够得到的无界等待是 `shm_transport.cpp:341`：

       WaitForSingleObject(impl.mutex, INFINITE);      // ShmChannel::publish()

   于是任何持有该通道命名互斥体的一方都能把它按死在这里。`publish_within_deadline`
   （`test_shm.cpp:65-80`）救不了：卡死发生在 `publish()` **内部**，它回不到那句
   `while (!channel.publish(...))` 去检查截止时间；`:135`、`:175`、`:213` 三处更是连那层包装都没有。

   `.scratch\_mutex_hold_ab.py` 的对照：

   | 条件 | 结果 |
   | --- | --- |
   | 没人碰那个互斥体 | 0.97 s 跑完，rc=0 |
   | 另一个进程按住 `Local\nav2shm_test_timeout_mutex` 8 s | 2 s 时进程还活着、**CPU 0.016 s**，卡在第一次 `publish`（`write_seq` 始终为 0） |
   | 按住的一方放开 | **立刻恢复**：rc=0、8.75 s、末行 `=== ALL TESTS PASSED ===` |

   所以「0 CPU + 永远不回来」这个签名有了一个**已证实**的成因，而且它**不是随机时序**：
   谁先拿到那个名字谁说了算。名字是 `Local\nav2shm_<用例>_*`（`shm_transport.cpp:199-201`），
   用例名是 `test_shm.cpp` 里的**固定常量**，`NAV2_SHM_NAMESPACE` **不作用**在这些名字上
   （它只作用在 shim 由话题名派生的通道名上），`Local\` 又按登录会话而不是按测试运行分段。
   于是同一台机器上第二个碰这个名字的进程 —— 并发的另一次运行、上一轮留下的进程、
   `bridge\windows\build` 里那份**同名**的 `test_shm.exe` —— 都能把它按死。
   另外 `test_cross_process`（`:335-390`）用 `CreateProcessA(...)` 起两个子进程且**没有分配
   job object**，ctest 超时只杀它自己启动的那一个，子进程会活下来继续占着 `test_xproc`。

   还有一条**还没证实**的隐患：事件（`..._event`）是**整条通道共用一个**的，
   `ResetEvent`（`shm_transport.cpp:416`）与 `SetEvent`（`:355`）配对，而 `:417` 那句
   「reset 之后再看一眼 seq」只保护做 reset 的那个读者；`test_broadcast_threads`
   （`test_shm.cpp:92-156`）正好是两个读者共用一个事件。它要成真得让一个读者**永久**掉队，
   目前既没观测到、也没排除。

   **一条方法论上的坑，留给下一个人**：`WaitForSingleObject` 对互斥体不是「看一眼」，而是**获取**它。
   我第一版探测器正是拿它去「查询」互斥体状态的，结果自己把被测进程按死了 161 s
   （0 CPU、`write_seq` 停在 0）—— 上面这条发现就是这么撞出来的。探测要么当场 `ReleaseMutex`，
   要么就别用互斥体做查询。

   第 9 轮那个 813 s / 1.11 s CPU 的实例**具体是谁持着锁，还没查出来**（当时并发的只有另外
   7 条 ctest 和守护进程，它们用的名字不同）。要把「自然发生」这一半钉死，需要能取挂住进程的
   线程栈，而仓库里目前**没有任何调试 / 栈采样手段** —— 这本身也是一条缺口。

   顺带量到一条真实性质：**这个环是「丢最旧」的有损环，不是可靠队列。** 跨进程用例里后起的读者
   实测丢掉 1024 / 3436 / 10868 … 最多 **48 976 条（50 000 条里 98%）**，测试本身也只断言
   "caught up to 50000/50000"。ROS2 的 `reliable` QoS 语义在这条通道上不存在，
   任何「每条都要到」的用法（rosbag 录制、TF 完整性）都不能直接建在它上面。

**2026-09-17：三条都改掉了** —— 第 1 条（读者耐心）和第 3 条（传输层）整条改掉；第 2 条（图注册表
丢写）先改掉「本进程自己的视图」那一半，**跨进程那一半在 2026-09-17 深夜也关上了**（见第十六节）。
三条都有同机 A/B 或者确定性复现脚本的对照，见证据 43。第 2 条那两个脚本仍然是它的判定标准：
`.scratch\_partial_graph_hold.py` 改之前必须红、改完必须绿（现在是绿）。

第 3 条改了两处：

* `ShmChannel::publish()` 里那个 `WaitForSingleObject(impl.mutex, INFINITE)` 换成 **5 s 有界等待**
  （`shm_transport.cpp:341` 附近）：拿不到锁就当这次写失败返回 `false`，交给调用方
  （`publish_within_deadline` 会报出来，nav2 的发布门控会记一笔）—— **挂死变成一次看得见的失败**。
  同机对照（`.scratch\_hold_probe.py`，从外面按住同一个互斥体 30 s）：改之前整进程被按住
  **30.75 s** 一动不动、按住方放手才恢复；改之后 **6.92 s** 就自己结束、rc=1，而且失败点指名道姓
  （`test_shm.cpp:213/214/215`）—— **在按住方还没放手的时候**。隔离命名空间下连跑 25 次，25 次干净。
* 通道名接上 `NAV2_SHM_NAMESPACE`（`test_shm.cpp` 里新增 `channel_name()`，5 处用例名），并给
  `shm_transport` 这条 `add_test` 补上 `ENVIRONMENT_MODIFICATION` —— 它原来是整棵树里**唯一**没有
  隔离的那条（同更正：准确说法是「shim 这棵树里唯一」，`bridge\windows` 当时也没隔离，
  见第二节第 8 条）。
  校验办法是把「旧名字」按住、同时让被测进程带着命名空间跑：`OpenMutexA` 直接
  **找不到**那个互斥体（`error: 2 = ERROR_FILE_NOT_FOUND`）、测试 0.94 s 全绿；反向对照（按住带
  命名空间的名字、进程也在同一个命名空间里）则是 6.95 s 失败。两次合起来说明名字真的换了。
  仍然成立的一条：**同一个命名空间的两次并发运行还是会撞**，但现在撞的结果是 5 s 后失败，不是挂死；
  彻底隔开需要「每次运行唯一」的名字，CTest 的 `ENVIRONMENT_MODIFICATION` 给不了。

**2026-09-17 晚补记**：整树 `ctest` 里 `shm_transport` 挂过一次 **35 min**（`test_shm.exe` 不返回，
被超时杀掉）。随后在默认命名空间与隔离命名空间里各单独跑一次都是 **0.9 s、rc=0**；再用
`.scratch\_ctest_tree.py bridge\windows\build`（钉 6 大核）复跑整棵桥接树 4/4、2.10 s 干净。按上面
「同一个命名空间的两次并发运行还是会撞」那条，判为**环境性偶发**（上次并行 session 的残留），
但它仍然是这套测试里最脆的一条：真去查的时候先看 `test_shm.exe` 是不是卡在命名互斥体上 ——
5 s 有界等待那处覆盖的是 `publish()`，不是测试自己的 setup/teardown。

### 八、按「现在挡着什么」排序

1. **跨机编解码表**（最硬的一堵墙，也是上面所有「用不了」的根）：85 种类型 vs 319 种生成类型
   （话题 38 / 216 = 17.6%、service 38 / 83 = 45.8%、action 9 / 20 = 45%）；一条活的 nav2 栈
   73 条话题 / 32 种类型**已经全部**在表里（2026-09-17，证据 48），**nav2 的 20 个 srv 已经全认了**
   （2026-09-17，证据 52 / 53），剩下 19 action 仍只认 8。
2. ~~**上行白名单**~~ —— **2026-09-17 关上了**：默认表多了 nav2 回话的 `/plan` 与
   `/behavior_tree_log`，不写参数就能上去（证据 54：`uplink=5`、`/plan msgs=16`、
   `/behavior_tree_log msgs=15`、`failed=0`，机器人侧逐字段解出 `/plan`）。剩下的半边是
   **成本**：上行没有节流、没有正则选择，两张 costmap 要显式 `--uplink`。
   **2026-09-17 晚：这一条现在有门禁了** —— 当时验收里量上行的仍然只有 `/cmd_vel`，旧的三名字
   默认表能拿全绿；新检查 `uplink_nav2` 要求默认表武装 `/plan` + `/behavior_tree_log`，并要求
   `/plan` 真的转发成功（证据 56。旧表下 5/6，只有它红）。
3. **`rclpy` / `rosbag2` / `launch` / `rclcpp_components` 全是零**（全仓库只有文档提过它们，
   代码里 `rclcpp_components` 只有 1 个「注册宏」头文件，没有 component container）。
4. **对端不是 ROS2**：现在这套验收证明的是「Windows 端的兼容层能跟一个 Python 重写体对话」，
   还没证明「Windows 端能进一个真实的 ROS2 图」。
5. **SLAM 的周边**：Ceres 解算器缺席、~~9 条 srv 过不来~~（2026-09-17 补全，第十七节）、`merge_maps_kinematic.exe` 的**合并语义**没验过（两个服务的通路 2026-09-17 已验，第十七节）、`save_map` 内部那条 `system("ros2 run ...")` 在本机必失败（改用 `map_saver_cli.exe`）、建图与导航的先后顺序没写进 runbook。
6. **工程化**：~~没有 git~~（2026-09-17 `git init` + `git add -A`，第二十节；**首次提交仍留给
   仓库所有者**）、**CI 有文件但一次都没跑过**（`.github/workflows/cross-platform.yml` 在等一个
   远端，而这个仓库还没有 remote），测试清单本身已经和文档对不上（见第二节）。
7. **可靠性：图注册表 + 传输层（`ctest -j 8` 十轮里挂 3 轮，单测也能挂）** ——
   这是唯一一类挡在整条链路**最底层**的缺口（第七节）：

   三条里**两条已经在 2026-09-17 改掉，只剩第一条**（改法与复测见第七节末和证据 43）：

   - ~~（改掉一半）~~ **（改掉了）** 一次被拒绝的图文件交换会丢掉那条注册，而 nav2 / costmap /
     slam_toolbox 的门控读的就是它（可随时复现；严重时 SLAM 一条地图都不发）。两半都关上了：
     `collect()` 走到本进程自己那份文件时改从内存取（两个复现脚本都从红转绿），而**别的进程**
     那一半由 `flush_locked()` 的重试保证 —— 它检查 `install_file()` 的返回值，失败就交给一个
     重刷线程**重新写 `.tmp` 再换**，所以丢写不再是永久的（第十六节）。读者仍然会在文件被按住
     的那段时间里看到上一版，这是「文件即通道」的固有性质；变掉的是它不再**永远**停在上一版；
   - ~~`ShmChannel::publish()` 在命名互斥体上 `WaitForSingleObject(..., INFINITE)`~~ →
     **5 s 有界**（`shm_transport.cpp:341`）：按住 30 s 的对照里，从「被按住 30.75 s 不回来」
     变成「6.92 s 自己失败退出」，失败点写到具体那一行；
   - ~~读者的 60 ms 墙钟预算~~ → **跟着错误码走**（名字不在 60 ms、被占 1000 ms）：同机 20 轮里
     2 轮丢读数 → 20 轮 0 轮。另外通道名也开始跟命名空间走（补上了那条唯一没隔离的 `add_test`），
     同命名空间的两次并发运行仍会互相干扰，但结果是**失败**而不是挂死。

   这一类不报错，只表现成「车不动 / 没有地图」，会让前六条在真机上随机失效 —— 所以三条都改掉了
   （最后一条见第十六节），没有一条是「排期处理」。

### 九、复修：三条里改掉两条，以及改完之后重跑的四条验收（2026-09-17 晚）

第七节那三条里，**第 1 条（读者耐心）和第 3 条（传输层）整条改掉；第 2 条（图注册表丢写）改掉了
「本进程自己的视图」这一半，跨进程那一半当时还开着 —— 那一半后来在第十六节关上了**。
改完在同一台机器、同样钉 6 大核的条件下重新量了一遍，全部是第一手（细节见证据 43）：

| 套件 | 命令 | 复修前 | 复修后 |
| --- | --- | --- | --- |
| 兼容层 + 魔改 nav2 | `.scratch\run_ctest.py 1` | 27/27、53.88 s | **27/27、50.65 s** |
| 桥接层 | `ctest --test-dir bridge\windows\build` | 4/4、2.64 s | **4/4、2.56 s** |
| 共享内存通道 | `ctest --test-dir nav2\shm_transport\build` | 1/1、0.97 s | **1/1、1.01 s** |
| `ros2_shim_graph_rewrite` × 20 轮 | `.scratch\_patience_ab.py` | **18/20 干净** | **20/20 干净** |
| `shm_transport` 被外部按住 30 s | `.scratch\_hold_probe.py ... 30` | **30.75 s 不回来** | **6.92 s 失败退出** |
| `slam_toolbox_mapping` 的复现脚本（按住自己的图 2 s） | `.scratch\_partial_graph_hold.py` | **rc=1、`:177` 挂** | **rc=0、all checks passed** |
| 同一个脚本的加强版（按住 6 s） | `.scratch\_greedy_graph_hold.py` | **rc=1、`0 map messages`** | **rc=0、239 条 map、279 occupied** |
| `.scratch\ctest_stress.py` --rounds 10 --jobs 8（27 条全跑、钉 6 大核） | `ctest -j 8` × 10 轮 | **7/10 轮干净**（`ros2_shim_graph_rewrite` 挂 1 次、`slam_toolbox_mapping` 挂 1 次，另 1 轮卡到 900 s 超时） | **10/10 轮干净、0 次失败** |

四条验收也在这之后现场重跑了一遍（逐条输出在证据 43 第四节）：

1. `ros2_bridge.bat start` → `ros2.bat topic list`：`/clock /cmd_vel /map /odom /scan /tf /tf_static`；
2. Windows 发布 → Ubuntu 订阅：`ros2.bat topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.25
   --duration 3` 之后，对端 `/sim/status` 回的是 `cmd=(0.25, 0.00)`，x 从 0.874 走到 1.612
   （≈0.25 m/s × 3 s）—— 这在验收上比「订阅者数 > 0」硬，因为它是**车真的动了**；
3. SLAM：`slam_demo.bat` 建图，四段「2 m + 90°」的闭环之后 `occupied=372`，地图经 `map_dump.exe`
   落成 PGM（`.scratch\audit_slam_map4.pgm`）；
4. nav2：`nav2_stack.bat --no-amcl --goal "1.0,-1.0"` → **`SUCCEEDED`**、5.60 s、1.106 m、
   `cmd_vel` **111** 条、plan 6 帧 / 11 个位姿、recovery 0；跑完之后同一张图 `occupied` 372 → 456。

顺序仍然是第五节那条：**先让 SLAM 把图建出来，再让 nav2 在上面规划** —— 反过来（拿一张空图
直接 `--goal auto`）拿不到目标点，1.7 s 就放弃。

### 十、`/rosout` 补上了：本机有日志话题，跨机转发也验过了（2026-09-17 晚）

B 节里那条「本机根本没有 `/rosout`」在这一轮补上了（细节与实录见证据 50）。改动落在
`nav2\ros2_shim\include\rclcpp\{logging,node,node_options,qos,shm_config}.hpp` 五处：

* **本机 `/rosout` 已有写者**：`Node` 构造末尾建 `rcl_interfaces/msg/Log` 的 publisher，
  `RCLCPP_*` 除 stderr 之外再发一条，`file` / `function` / `line` 由宏带上（宏多带三个参数）。
  分流按 logger 名选 sink，没有 sink 承接的就地丢弃；`enable_rosout(false)` 或
  `NAV2_SHM_ROSOUT=0` 时整个进程既不建 publisher 也不发日志。实测：`rosout_probe.exe` 的 12 条，
  以及 nav2 六个服务器自己的启动日志，都能在 `ros2 topic echo /rosout` 里读到，`file` / `line`
  指向的是**上游 nav2 源码的行号**。
* **`/diagnostics` 仍然是缺口**：没有任何本机写者，要等 `diagnostic_aggregator` 这类节点被移植
  （`diagnostic_msgs/msg/DiagnosticArray` 的 codec 早就在表里了）。

* **跨机转发已经实跑过，而且机器人侧连字段一起读**：`--uplink /rosout` + 机器人侧 
  `run_sim.py --subscribe /rosout`，机器人侧收到 `probe line 1 of 60` 到 
  `probe line 60 of 60`，**一条不差**，`level` / `line` / `file` / `function` 都解得出来。
  这一轮给仿真包 `ros2_bridge_sim/messages.py` 补了 `RCL_LOG` 布局（对着 `rosbags` 
  逐字节验过，样本表 6 → 7 个类型）。剩下的边角是**时序**：上行通道是
  「守护进程先看见本机有人发这个类型的话题」才建的，建好之前落在环里的消息
  按 volatile 语义就没了（通道建好前发的那一条 `msg=warm` 在机器人侧是 0 条，
  建好之后的 60 条全到）。这不是 `/rosout` 特有的。

三条新的 caveat（都写进了证据 50 第八节）：

1. **整机共用一条 8 槽环、volatile**：`/rosout` 跟别的消息抢同一批槽 —— `NAV2_SHM_SLOT_COUNT`
   是**全话题共用**的一个数，单给 `/rosout` 加大会连带所有话题占内存。attach 之前发的日志读不到；
   日志一多还会**挤掉别的消息**。上游是每节点一个 publisher、深度 1000。
2. **DEBUG 与 stderr 共用一道闸门**：`NAV2_SHM_LOG_LEVEL` 调大，两边一起变大，做不出「控制台
   安静、话题上全量」。
3. **拒收看不出来**：`enable_rosout(false)` 的节点在 `ros2 topic info /rosout` 里和别的节点长得
   一样，只能靠「`echo` 少了谁的 `name`」判断。

另外这一轮还压出一个**生成器**层面的坑，值得单独记：`windows.h` 把 `ERROR` / `DELETE` 定义成宏，
而生成的消息头里有同名常量（`rcl_interfaces/msg/Log`、`diagnostic_msgs/msg/DiagnosticStatus`、
`visualization_msgs/msg/Marker`、`map_msgs/msg/PointCloud2Update` 四个文件）。撞上之后那个头
**自己不报错**、变成 `40 = 40`，报错的是**用**它的翻译单元（`Mapper.cpp`、`tests\map_dump.cpp`、
`tests\tf_probe.cpp`）。修在生成器里（`generate_messages.py` 的 `macro_guard_lines()`，撞名才
插 `#undef`），并用「临时目录跑生成器 + 逐字节 diff」复核：442 个定义里只有那 4 个不同，差异恰好
就是 `#undef` 段。`prelude.hpp` 里原本那句 `#undef ERROR` 管不到 `nav2\ros2_shim\tests\` 那些
不经过 `prelude.hpp` 的翻译单元 —— 这是它真正的盲区。

同一轮的测试数字（全部钉 6 大核）：兼容层 + 魔改 nav2 **29/29、48.27 s**（28 → 29，新增
`ros2_shim_rosout` 0.65 s）、桥接层 **4/4、2.10 s**、仿真包单测 **61 passed**、验收 **6/6**
（`.scratch\acceptance_round12.json`）。`shm_transport` 在整树 `ctest` 里挂过一次 35 min，判定与
复测见第七节末尾那条补记。

## 十一、测试树审查：两条「编了但从来没注册」的测试，和一个会骗过验收的仿真底座（2026-09-17 晚）

这一轮的完整审计单独成篇：**`05-audit.md`**（测试树普查、四棵树的实跑、缺口逐条重量）。
这里只记两件会改变「还缺什么」的东西。

1. **`py_interop` 与 `test_nav2_ros_common` 从来没进过 ctest。** 两条都有源码、有 `main`、
   有可执行文件，33 个 `add_executable` 里就这两条没有对应的 `add_test`。`py_interop` 恰好是
   **整个 shim 最底层的那条假设**（两边说的是同一种 CDR）的四路对照，所以这条漏洞的性质不是
   「少跑一条测试」而是「一条核心契约可以静默烂掉」。补法：`test_nav2_ros_common` 加一行
   `add_test`；`py_interop` 加一个 Python 驱动
   （`nav2\ros2_shim\python\interop_matrix.py`，五条腿：cpp dump→py check、py dump→cpp check、
   两份 dump 逐字节相同、cpp pub→py sub、**py pub→cpp sub**）。测试树因此 29 → 31。
   补注册时还发现 `test_nav2_ros_common.cpp` **一条断言都没有**（整个 `main` 就是
   建节点、建 pub/sub、走四个状态、`return 0`），也就是「注册了」不等于「在测东西」；
   同一个文件现在有 **15 条断言**（QoS 三个字段、图里的 pub/sub 计数、四个生命周期状态、
   回调次数），并且做过变异验证（改一条断言 → `FAILED (1 failure(s))`、`rc=1`）。

2. **仿真底座没有 `cmd_vel` 看门狗**（`bridge\ubuntu\ros2_bridge_sim\sim.py:102-117`）。
   `_on_cmd_vel` 只把值存进 `self.linear/self.angular`，积分循环永远用它、**没有任何超时**。
   实测：一条 `--once` 的 0.2 m/s 命令让车一路开到 x=102.52 m，四秒采样 +0.812 m（0.203 m/s），
   再发一条 0 才停。两个后果：**（a）「小车在动」不构成任何证据** —— 上一轮 demo 留下的命令
   会让它一直动，这一轮我第一次跑 SLAM 就是因此拿到一张 8x5 全 unknown 的空图（车已经开出
   房间 100 m，雷达什么都看不到）；**（b）真实部署里挡这件事的是 `velocity_smoother` 的
   `cmd_vel_timeout` 与 `collision_monitor`，仿真底座这一层什么都没有**，所以整条链路上没有
   任何东西会发现「发布者已经死了，车还在跑」。规避：跑 demo 前先发一条 0，或用 `/sim/reset`
   （`acceptance_check.py` 收尾就会 reset）。真正的修法是给 `set_cmd` 加一个可配的看门狗。**2026-09-17 深夜已修**：0.5 s 看门狗 +
   7 条 pytest，以及它随后撞出的两件事，见 `05-audit.md` 第十节。

同一轮的测试数字（全部钉 6 大核；**五棵树，101 条**）：兼容层 + 魔改 nav2 **31/31、52.72 s**
（29 → 31）、桥接层 **4/4、2.27 s**、**gRPC SDK 4/4、1.68 s**、`nav2\shm_transport` **1/1、1.05 s**、
仿真包单测 **61 passed、21.33 s**、全链路验收 **6/6**（`.scratch\audit_acceptance.json`，含一条
新的跨进程复核：存档 → 另一个进程 `--mode localization` 读回来 473x473、426 个占据格）。

这一轮还补了两条审计动作：**把守护进程停掉再跑一遍 31 条**（31/31、53.04 s —— 排除了
「测试靠桥上活数据喂」这种解释），以及 **`ctest -j 8` 五轮**（5/5 干净、每轮 19.5-22.8 s；
历史那条「十轮里 7 轮干净」是并行的**失败史**，不是今天的状态）。两棵我第一遍漏掉的树
（`gRPC\ros2_gRPC_sdk` 4 条、`nav2\shm_transport` 1 条）也补进了 `05-audit.md` 第三节。

## 十二、`shm_transport` 的 POSIX 后端补上了（2026-09-17 晚）

这一条以前是这么写的：**共享内存传输只有 Windows 实现，POSIX 分支是「未实现」的桩**。现在不是了，
而且实际情况比「桩」还差一层——那份代码在 Linux 上**连编译都过不去**：

* `create` / `open` 返回 `fail("shared memory backend not implemented for this platform")`，
  `publish` / `subscribe` 直接 `return false`（原来在 `shm_transport.cpp:267-270 / 326-329 / 374-378 / 452-458`，
  这四处在改之前是整份文件里**唯一**的 `#ifdef _WIN32` 之外的东西）。
* 更要命的是 C ABI：`shm_transport_c.h` 导出了 `shm_open(const char *)`，而 POSIX 的 `<sys/mman.h>`
  里就有 `shm_open`。第一次在 Ubuntu 上 configure + build 的实际输出是
  `error: conflicting declaration of C function 'shm_channel_s* shm_open(const char*)'`（g++ 13.3）。
  这个符号即使在 Windows 上编出来也不该留在 `.so` 里：谁在它后面加载，谁的真实 `shm_open` 就被它接了。

改法是**把算法和平台分开，而不是写第二份实现**：`publish` / `subscribe` / 环形游标 / CRC /
`seek_to_latest` / `seek_to_latched` 现在只有一份（`shm_transport.cpp:765`、`:808`），平台差异收在
`ShmChannel::Impl`（`:176`）的几个小函数里：`lock_writer`（`:216` / `:293`）、`wake_readers`（`:229` / `:324`）、
`prepare_wait`（`:234` / `:338`）、`wait_for_wakeup`（`:239` / `:342`）、`close`（`:255` / `:384`）。

| 要同步的东西 | Windows | Linux / POSIX |
| --- | --- | --- |
| 通道 | `CreateFileMapping(Local\nav2shm_<名>_data)` | `shm_open("/nav2shm_<名>_data")` + `ftruncate` + `mmap` |
| 写者锁 | 命名互斥体 + 5 s 有界等待 | 段内 `pthread_mutex_t`（`PTHREAD_PROCESS_SHARED` + `ROBUST`）+ `pthread_mutex_timedlock` |
| 唤醒读者 | 命名手工事件（`SetEvent` / `ResetEvent`） | 段内 `pthread_cond_t` 广播 |
| 段的生命周期 | 最后一个句柄关掉时内核销毁 | 段本身的 `flock` 共享锁，最后一把锁的人 `shm_unlink`（原先是段内引用计数，第十三节换掉了） |
| 时钟 | `GetTickCount64()` | 两边统一成 `steady_clock`（`mono_ms()`，`:104`） |

四处设计决定，都不是随手挑的：

1. **唤醒用条件变量广播，不用信号量**：`sem_post` 只唤醒一个等待者，而这条通道的契约是
   「任意多读者各自游标」（`shm_channel.hpp` 开头）。两个读者的一条通道，信号量会漏掉一个。
   同步对象住在共享段里（`ShmSync`，`:135`，`PTHREAD_PROCESS_SHARED`），所以 `kHeaderSize` 两个平台
   不一样：Windows 是「layout 头对齐到 64」（`:119`），POSIX 还要加上 `ShmSync`（`:148`）。这个值写进
   头部，`open` 用同一个值算槽位偏移，读方不需要知道对方是怎么算的。
2. **写者锁建成 ROBUST**：这不是锦上添花。第七节那条 161 s / 0 CPU 的挂死就是「持有者不放」，
   Windows 侧靠 5 s 有界等待把它变成一次看得见的失败；POSIX 侧同样的等待之外，持有者**死掉**
   （`kill -9`、崩溃、上一轮留下的进程）会留下 `EOWNERDEAD`，下一个写者接管并
   `pthread_mutex_consistent`，不用等人来清。
3. **段的生命周期一开始靠引用计数，后来换成了 `flock`**：Windows 的命名对象随最后一个句柄消失，
   POSIX 的段却会一直躺在 `/dev/shm` 里直到有人 `shm_unlink`。最初的办法是在段里维护一个
   `ShmSync::users` 计数，最后一个 `ShmChannel` 退出时 unlink。它有一个当时没看出来的洞：**被
   杀掉的进程不会减这个计数**，于是名字永远回收不掉，下一轮 `create` 收编到的是尸体——环里是
   上一轮的消息，互斥体是死线程留下的状态，然后就挂死在第一节 `publish` 上（第十三节有全过程）。
   现在改成段本身的 `flock` 共享锁：活着的每人一把，内核在进程结束时收回，`create` 先要独占锁，
   拿得到就是「没人在用」，把段里的东西全部重新初始化；拿不到就原样收编。语义和 Windows 的
   句柄生命周期一一对应，而且没有「计数与 unlink 之间那个微秒级窗口」了。
4. **`prepare_wait()` 在 POSIX 侧是空的**：第三节第 2 条记过一个「两个读者共用一个事件」的隐患
   （reset 之后再看一眼 seq，只保护做 reset 的那个读者）。条件变量没有可被消费的latch 状态，
   判断改在 `wait_mutex` 里做，所以那条隐患在 POSIX 侧不存在。Windows 侧的语义**一行没动**。

实测（`NAV2_SHM_NAMESPACE=ctest_shm_transport`，全部钉 6 大核）：

| 项 | 结果 |
| --- | --- |
| Ubuntu 24.04 / g++ 13.3 / cmake 3.28 编译 | `-Wall -Wextra` **0 warning**；CMake 自查 `Looking for shm_open in rt - found`，只有老 glibc 才链 `-lrt` |
| `./test_shm`（5 条用例） | **全绿**：超时 **200 ms**（断言 150~2000）、慢读者 **8 / 92**、广播 1 写 2 读 **20000/20000**（droppedA=0 droppedB=0）、吞吐 100 万条 **479 ms（2 087 683 msg/s）**、跨进程 50000 条 **`writer exit=0 reader exit=0`** |
| 连跑 5 次 | **5/5 全绿**（新同步路径没有「偶尔挂一下」） |
| 跑完的 `/dev/shm` | `ls /dev/shm \| grep -c nav2shm` = **0**（引用计数确实收干净了，没有残留段） |
| `ctest`（Ubuntu） | **1/1、0.83 s** |
| 对照：同一棵树 Windows | `python tools\run_ctest.py shm` **1/1、0.94 s**；吞吐 100 万条 **611 ms（1 636 661 msg/s）** |
| 回归：另外两棵也重编重跑 | `nav2\ros2_shim` **31/31、53.96 s**、`bridge\windows` **4/4、2.43 s**（`shm_transport` 是被这两棵树 `add_subdirectory` 进去的，它们三个共用同一份源码） |

`test_cross_process` 的 POSIX 分支原来是 `skipped (windows only)`，现在是真跑：`/proc/self/exe` +
`fork` + `execv` + `waitpid`，父进程留一个 keeper 通道——「最后一个用户 unlink」的语义要求有人一直
拿着段，这一点和 Windows 侧「父进程留一个句柄让命名对象活过子进程」是同一个原因。

顺带一个**破坏性改动**：C ABI 全部加了 `shm_channel_` 前缀（`shm_channel_create` / `shm_channel_open` /
`shm_channel_publish` / `shm_channel_subscribe` / `shm_channel_*_count` / `shm_channel_close`）。
C++ 侧（`shm_transport::ShmChannel`）没变，`nav2\ros2_shim` 和 `bridge\windows` 走的都是 C++ 接口；
仓库里唯一用 C ABI 的是 `nav2\shm_transport\python\shm_transport.py`，已经一起改了（ctypes 冒烟通过）。

**还缺什么**（这一条本身仍然是缺口）：

* **CI 有了，但还没在 runner 上跑过**：`.github/workflows/cross-platform.yml`（`windows` job 用
  setup-msys2 的 UCRT64，`linux` job 用 apt 装 cmake / ninja / zlib / eigen / boost / grpc /
  protobuf / yaml-cpp），两边跑同一串入口命令。仓库没有 git 远端可推，所以到写这段为止它**一次都
  没被执行过**——「两边都能编」的证据仍然是本节这张表和第十三节那张表，不是这条门禁。
  `gRPC\ros2_gRPC_sdk\build\platforms.yaml` 里也仍然只有 `windows-x86_64` 一条。
* **构建入口不再是挡路的东西**（第十三节做完的）：`nav2\ros2_shim\build.py`、
  `bridge\scripts\build_bridge.py`、`tools\run_ctest.py`、`tools\ctest_stress.py`、
  `nav2\shm_transport\build.py` 都改用共享的 `tools/_toolchain.py` + `tools/_cores.py`，没有写死的
  MSYS2 路径了。`ros2_shim` 的全量目标已经在 Ubuntu 上编过（`BUILD_DONE`，`grep -c 'error:'` = 0）。
  还留着复制品的只有 `gRPC\ros2_gRPC_sdk\scripts\build.py`（自带 `DEFAULT_MSYS_ROOT` /
  `big_cores()`），以及 `bridge\windows` 在 Linux 上编一次这件事本身。
* Linux 侧验过的（第十三节）：`MAP_SHARED` 落在 tmpfs 之外的文件系统（Alpine chroot 的
  `/dev/shm` 是 ext4 目录）、`PTHREAD_MUTEX_ROBUST` 在 musl 上的行为（可用，5/5 全绿）。
  仍然没验的：两个 **Linux 进程跨机器**共享内存这件事本身（对端不在一台机器上时这条通道就是
  不成立的，跨机只能走 gRPC 那一层）。

## 十三、跨平台构建入口 + 三平台实测：又撞出一个真死锁（2026-09-17 深夜）

这一节把第十二节末尾「还缺什么」那条清单执行掉了，顺序是：先把构建入口跨平台，再在 Windows
和 Ubuntu 上各编一遍跑一遍，顺手补 CI，最后把 musl 和「`/dev/shm` 之外的文件系统」这两条未验项
验掉。**过程中撞出的那三个缺陷是这个阶段真正的产出**，两个编译错误一个死锁，全部在兼容层 /
传输层里，上游 nav2 源码一行没动。

### 一、构建入口不再是「Windows 专用」

工具链解析收进 `tools/_toolchain.py`（`find_tools()` / `build_env()` / `describe()`），核数收进
`tools/_cores.py`（`big_cores()` / `pin()`，仍然是 Windows 的 `{0,1,10,11,12,13}`、mask `0x3C03`）。
调用的五处脚本一起改了：`nav2\ros2_shim\build.py`、`bridge\scripts\build_bridge.py`、
`tools\run_ctest.py`、`tools\ctest_stress.py`，加上新写的 `nav2\shm_transport\build.py`
（这一棵树原先没有入口脚本，只能手敲 cmake）。可以覆盖的东西：`CPU_MSYS2_ROOT=` / `--msys2`、
`CMAKE=` / `NINJA=` / `CXX=`、`BIG6_CORES=` / `BIG6_NO_PIN=1`。

顺手补掉的两个坑：

| 坑 | 症状 | 现在 |
| --- | --- | --- |
| `libbehaviortree_cpp.a` 是没人记录的手工前置条件 | Linux 上 `ninja: error: ... missing and no known rule to make it` | `build.py` 自己先 configure + build 它（`--no-btcpp` 可跳过） |
| `-static -static-libgcc -static-libstdc++` 挂在 `NOT MSVC` 上 | Linux 上试图把 libc 也静态链，缺 `libc.a` | 收窄成 `MINGW`，附注释 |
| CMake 的 try-compile 找不到 `c++` | Alpine（只有 `g++`）报 `posix_spawn /usr/bin/c++: No such file or directory` | 脚本把找到的编译器显式写进 `-DCMAKE_CXX_COMPILER` |

门禁脚本 `tools/check_cross_platform.py`（本地 + 远端同一张表，`--quick` / `--local` / `--remote` /
`--push-only`），CI 在 `.github/workflows/cross-platform.yml`。**CI 一次都没在 runner 上跑过**
（没有 git 远端），这一点在仓库根的 README 里也如实标了。

### 二、Linux 全量编译撞出的两个真错误

`nav2\ros2_shim` 在 Ubuntu 上全量编（498 个目标），`ninja -k 0` 一次抓全，**只有 2 个真错误**，
都在兼容层里：

| 错误 | 上游为什么没这个问题 | 改法 |
| --- | --- | --- |
| `nav2_graceful_controller/src/graceful_controller.cpp:450`：`std::max(abs(vel.angular.z), params_->v_angular_min_in_place)` → `no matching function for call to 'max(int, double&)'` | 真 ROS2 的 include 集里传递性引入了 `<math.h>`，于是 `abs` 是浮点那个 | 兼容层的 `prelude.hpp` 补 `#include <math.h>`，并在注释里写清楚为什么是它而不是 `<cstdlib>` |
| `nav2_amcl`：glibc 上 `static declaration of 'drand48' follows non-static declaration` | 上游 `nav2_amcl/CMakeLists.txt` 用 `check_symbol_exists(drand48 stdlib.h HAVE_DRAND48)` 决定要不要编那份回退实现 | 兼容层用同一个问题问一遍，`PUBLIC` 传播给 `nav2_amcl_core_c`（`amcl_node.cpp` 是 C++，也要这个答案）。Windows 上实测是 `-- Looking for drand48 - not found`（正确：MinGW 需要回退），Ubuntu 上编过 |

编完之后：`grep -c BUILD_DONE /tmp/full2.log` = 1，`grep -c 'error:'` = **0**。

### 三、musl 与「非 tmpfs 的 `MAP_SHARED`」：验掉了

Ubuntu 上用 chroot 起了个 Alpine 3.24.1（`apk add build-base cmake ninja python3`）：

| 项 | 结果 |
| --- | --- |
| `shm_transport` 对 musl 编译 | 通过（`_toolchain.py` 正确报出 `cxx=/usr/bin/g++`） |
| `test_shm` | **全绿**，连跑 5 次 |
| 吞吐 | 1.58 → **1.96 M msg/s**（同一个 chroot，修掉下面那个死锁之后） |
| `PTHREAD_MUTEX_ROBUST` | musl 上可用 |
| `/dev/shm` | chroot 里是**普通 ext4 目录、不是 tmpfs**，`MAP_SHARED` 一样成立；跑完无残留 |

一个必须一起记的边界：Alpine 里**只有直接 `g++` 编得动**，CMake + ninja 会因为缺 `/proc`
（`Problem opening /proc/cpuinfo`）和 `posix_spawn /usr/bin/c++` 失败。所以「musl 上这条通道能用」
成立，「CMake 在 Alpine chroot 里能跑」不成立。

### 四、POSIX 后端的死锁：整条证据链

**症状**：`test_shm` 的吞吐用例挂死。Ubuntu 上只要有负载（后台在跑 6 核全量编译）就
**6/6 超时**，无负载时 Debug 树第 1 次就挂；Alpine 上 5 次里 2 次 `exit=143`。

**第一步（把「随机」变成「确定」）**：`/dev/shm` 里躺着上一轮被 `timeout -s KILL` 杀掉的段
`nav2shm_test_throughput_data`，`write_seq=1001830`。**带着这个段跑，一次就复现**；`ls /dev/shm`
里没有别的进程持有它（`fuser` 空）。也就是说：不是调度，是那个段本身。

**第二步（段内状态）**：把段 mmap 出来按 `pthread_mutex_t` 的布局读：

```
wait_mutex  lock=0xc0000000 count=1 owner=0x7fffffff nusers=3 kind=144
```

`kind=144` = `PTHREAD_MUTEX_ROBUST_NORMAL_NP | PTHREAD_MUTEX_PSHARED`（robust 是设上了的），
`lock` 的 `0x40000000` 位是内核的 `FUTEX_OWNER_DIED`，`0x80000000` 是有等待者，
`owner=0x7fffffff` 是 glibc「不可恢复」的哨兵值。

**第三步（活着的进程）**：复现后 `ps -L` 两个线程都停在 `futex_do_wait`、**0% CPU**；再读一次段：
`wait_mutex.lock = 0x801904ba`，低 30 位 `0x1904ba` **正是写者线程自己的 TID**，而写者正卡在
`__pthread_mutex_lock_full` 里等这把锁。自己等自己，而且是「不可恢复」那个状态，所以永远。

**结论**：写者的 `wake_readers()` 拿到 `EOWNERDEAD` 时把它当成失败直接 `return`——既没广播也没解锁。
`EOWNERDEAD` 对 robust 互斥体来说是**拿到了锁**，只是状态不一致，必须
`pthread_mutex_consistent()` + `pthread_mutex_unlock()` 才算还回去；不做，这把锁就归了一个不知道
自己拿着它的线程（而且 `__owner` 被留成哨兵值，连「自锁」都检测不出来）。触发链条只有两步：
**(1) 段内引用计数在进程被杀时不会减 → 名字永远回收不掉；(2) 下一轮 `create` 收编这具尸体 →
第一次 `publish` 就撞上死线程留下的 `wait_mutex`。**

**改法**（两处，都在 `nav2/shm_transport/src/shm_transport.cpp`，Windows 侧一行没动）：

1. `wake_readers()` 把 `EOWNERDEAD` 当成「拿到了」，先 `pthread_mutex_consistent()` 再广播再解锁；
   其它失败（`ENOTRECOVERABLE`）只是**跳过这一次唤醒**，`write_seq` 已经写进去了，读者自己会
   超时重看一眼，不能把写者卡死。
2. 段的生命周期从段内引用计数换成**段本身的 `flock` 共享锁**：活着的每个 `ShmChannel` 持一把共享锁
   （`open` 也持），`create` 先要独占锁——拿得到说明没人在用（内核在进程死掉时收回了所有锁），
   那就把环、互斥体、条件变量全部重新初始化；拿不到说明有活人在用，原样收编。收编时先拿共享锁，
   还能顺便挡住「另一个进程正在初始化这个段」的那几微秒。最后一个关闭的人在独占锁里
   `shm_unlink`，并且**先确认这个名字还指向自己持有的那个 inode**（`fstat` 比对）——否则会误删
   一个刚好在同一个名字上新建起来的段。

**回归**（`test_shm` 新增两条用例，正好对着这两条规矩）：

| 用例 | 验的是 |
| --- | --- |
| `a channel whose writer is gone comes up empty` | 子进程 `_exit`（不析构、不 unlink）留下段，父进程 `create` 必须起一个 **空环**（`published_count() == 0`） |
| `a channel a live writer holds is adopted, not reset` | 写者还活着时 `create` 必须**原样收编**（`published_count() == 5`），且段活过写者退出 |

| 平台 | 修之前 | 修之后 |
| --- | --- | --- |
| Ubuntu 24.04，带着那个尸体段 | **挂死**（一次就复现，`timeout` 45 s 杀掉；去掉段才不挂） | **5/5 全绿**，其中一次是在 6 核全量编译负载下；跑完 `ls /dev/shm \| grep nav2shm` = 0（尸体也被回收了） |
| Alpine / musl，连跑 5 次 | 2 次 `exit=143`（超时），留下残留段 | **5/5 全绿**，吞吐 1.96 M msg/s，无残留 |
| Windows / MinGW（对照） | 5 条全绿 | 5 条全绿，连跑 3 次 |

吞吐也回来了：Ubuntu 无负载 **2.18 M msg/s**（挂死状态下的那种「掉到 1.5 M 并且抖」不见了）。

### 五、`bridge\windows` 在 Linux 上：0 error + 4/4

上一稿这一节写的是「还没编过一次」，还推了一个原因：「POSIX 侧要 pid 才能 kill，
得在 `RegisterAgent` 里加 `int32 pid`」。**那个推测是错的**：`ros2_bridge.proto` 里根本没有
`RegisterAgent` 这个 RPC，`detach_process` 也跟 kill 没关系。真正挡住它的是两件事，一件在
CMake 里，一件是一行 `#include`。

| 平台 | configure | 构建 | ctest |
| --- | --- | --- | --- |
| Windows / MSYS2 UCRT64 | 走 `ProtobufConfig.cmake`（`Protobuf_DIR` = MSYS2 的 `lib/cmake/protobuf`） | 0 error | **4/4**：`bridge_cdr_roundtrip` / `bridge_service_roundtrip` / `bridge_action_roundtrip` / `shm_transport` |
| Ubuntu 24.04 / g++ 13.3 / cmake 3.28 | 走 CMake 自带的 `FindProtobuf` 模块（系统 protobuf 3.21.12） | 0 error | **4/4** |

**1. `find_package(Protobuf CONFIG REQUIRED)`**。MSYS2 带 `ProtobufConfig.cmake`，Ubuntu 的
`libprotobuf-dev 3.21.12` **一个 CMake config 都不带**：`/usr/lib/x86_64-linux-gnu/cmake/` 下只有
`grpc` / `absl` / `boost*` / `yaml-cpp` 这些，`find /usr -iname "*protobuf*cmake*"` 是空的。连同一目录的
`gRPCConfig.cmake:9` 自己都在退回 `find_package(Protobuf)`。改成先 CONFIG、没有再 MODULE，
`Protobuf_DIR` 证明 Windows 那条路一个字没变。

**2. `#include <grpc/impl/channel_arg_names.h>`**。这个头是 gRPC 1.55 才有的；Ubuntu 的
`libgrpc++-dev` 还是 1.51.1，`GRPC_ARG_*` 这时候在
`grpc/impl/codegen/grpc_types.h`（由 `grpcpp/grpcpp.h` 带进来，`/usr/include/grpc/impl/` 下
只有 `codegen` 一个目录）。加一层 `__has_include` 守卫，两边同一个文件编过去。

**顺带补掉的全仓库唯一一处真桩**：`bridge_daemon.cpp` 的
`#else void detach_process(...) {}`。POSIX 侧的 `freopen` + `setvbuf` 和 Windows 侧本来就是
同一对调用，只有 `FreeConsole()` 和 `NUL` / `/dev/null` 的写法不同，所以合成一个函数而
不是留空壳：空壳会让 `--detach` 在 Linux 上惄惄变成「保留调用者的 stdout」，正是
Windows 那份代码要防的事（`ros2.bat topic list | more` 会挂到守护进程退出）。glibc 的
`freopen` 带 `warn_unused_result`，MinGW 的头里没有（Windows 侧同一写法一直没报过），
所以兜底那次的返回值被命名后丢掉，而不是直接忽略。

同一个 `find_package(Protobuf)` 问题在 `gRPC\ros2_gRPC_sdk\CMakeLists.txt:27` 也有一份，一起修了：
**通信 SDK 本身也第一次在 Linux 上 configure / build / ctest 全过（4/4）**。它的
`README.md` 里「Building on Linux」那一段写了好多轮，但按它做是 configure 就停。

### 六、这一节之后仍然没做的

* `.github/workflows/cross-platform.yml` **仍然没在任何 runner 上跑过**（仓库没有 git 远端）。
  它里面的每一步都在本机和那台 Ubuntu 上手工跑过 —— 跑的就是
  `tools\check_cross_platform.py` 那张表，现在多了 `grpc sdk: build entry point + ctest` 一行。
* 那份 workflow 的 `gRPC SDK` 那一步在这一轮之前是 `run: python tools/run_ctest.py grpc`，
  而 job 里没有任何一步 configure 过那棵树 —— 它在 runner 上必然失败。`run_ctest.py` 的 `grpc`
  解析也一起修了：SDK 是唯一按平台分子目录（`build/<platform-key>`）的树，之前
  `run_ctest.py grpc` 能过只是因为 `gRPC\ros2_gRPC_sdk\build\` 里还躺着一棵 2026-09-15 的旧树，
  跑的是两天前的二进制；现在按「哪个子目录真的有 `CTestTestfile.cmake`」解析。
* CI 里 linux job 的 apt 那串包是照着这台机器 `dpkg -l` 抄的，**没有在干净容器上验过**。
* `bridge\linux` 的启停脚本 **2026-09-17 深夜补上了**（四个 `.sh`，与 `bridge\windows`
  的四个入口一对一；pidfile 走 `setsid` + 回退扫 `/proc/<pid>/comm`，`stop` 等进程真的消失再返回）。
  在 Ubuntu 上实测 `start` → `ros2 topic list -t`（七条话题带类型）→ 上行 `msgs=19` → `stop`，见第十四节。
* 两个 Linux 进程**跨机器**共享内存这件事本身（跨机只能走 gRPC 那一层）。

## 十四、`bridge/linux`：Linux 上第一次有人把守护进程跑起来（2026-09-17 深夜）

第十三节末尾那条「`bridge\linux` 没有启停脚本」的成因说得很准：Linux 侧此前只跑 gRPC agent，
**没人在机器上起过守护进程**，所以 pidfile、`setsid`、怎么判断「它还活着」这一圈从来没被碰过。
这一节把它补齐，并且第一次让整条链路在 Linux 上真跑起来 —— 不是「编译通过」，是**话题真的出来了**。

### 一、四个脚本，一对一

| 脚本 | 对应 | 说明 |
| --- | --- | --- |
| `bridge/linux/bridge_env.sh` | `bridge_env.bat` | 必须 `source`；变量、默认值、承诺都和 .bat 一样，PATH 里多一个 `ros2` |
| `bridge/linux/ros2.sh` | `ros2.bat` | 一条命令用 ROS2：确保守护进程 + 等 ready + 转给 CLI |
| `bridge/linux/ros2_bridge.sh` | `ros2_bridge.bat` | `start` / `run` / `status` / `pid` / `stop` / `log [N]` |
| `bridge/linux/wait_bridge_ready.sh` | `wait_bridge_ready.bat` | 起守护进程并轮询 `ROS2_BRIDGE_READY_FILE` |

构建目录仍然叫 `bridge/windows/build`（`build_bridge.py` 的 `DEFAULT_BUILD`，两边同名），变的只是
二进制没有 `.exe`。`.sh` 沿仓库的既有约定不带头执行位，所以文档里一律写 `bash <脚本>`。

### 二、三处 Linux 专有的设计，都是被撞出来的

1. **`setsid` + pidfile，再加一层回退**。守护进程自己不 fork（`detach_process` 只做 `freopen` +
   `setvbuf`），所以得有东西给它一个新会话，它才活得比调用它的 shell 久。但 `setsid` 在子进程已经
   是进程组长时会 fork，`$!` 就不可信；所以 pidfile 只是第一条线索，找不到时按
   `/proc/<pid>/comm` 扫进程表。
2. **匹配进程名，绝不匹配命令行**。`pgrep -f ros2_bridge_daemon` 会把**跑这个脚本的 ssh 会话本身**
   匹配上（它的命令行里就有这个字符串），脚本会转身去停一个并不存在的守护进程。进程名被内核截到
   15 字节，所以比对的是 `ros2_bridge_dae`。
3. **`stop` 要等进程真的消失**。`kill` 立刻返回，而调用方下一句往往就是 `ros2 topic list`；
   Windows 那份 `taskkill` + 轮询的注释写的是同一件事，这里是 `kill` 加每 0.1 秒一次的 20 秒等待。

### 三、实测：Ubuntu 24.04 上真的跑通了

```
$ bash bridge/linux/ros2_bridge.sh status
[ros2_bridge] daemon not running, log: /home/ubuntu/code/CPU/bridge/windows/build/ros2_bridge_daemon.log
graph dir      : /tmp/nav2_shim_graph
topics         : 0 (0 remote, 0 local)
...
$ ROS2_BRIDGE_AGENT=127.0.0.1:50051 bash bridge/linux/ros2_bridge.sh start
[ros2_bridge] starting daemon --agent 127.0.0.1:50051 --ready-file /home/ubuntu/code/CPU/bridge/windows/build/ros2_bridge_daemon.ready
[ros2_bridge] started pid 1652093, log: /home/ubuntu/code/CPU/bridge/windows/build/ros2_bridge_daemon.log
$ bash bridge/linux/ros2.sh topic list -t
/clock [rosgraph_msgs/msg/Clock]
/cmd_vel [geometry_msgs/msg/Twist]
/map [nav_msgs/msg/OccupancyGrid]
/odom [nav_msgs/msg/Odometry]
/scan [sensor_msgs/msg/LaserScan]
/tf [tf2_msgs/msg/TFMessage]
/tf_static [tf2_msgs/msg/TFMessage]
$ bash bridge/linux/ros2.sh topic pub /cmd_vel geometry_msgs/msg/Twist "linear.x=0.0" --rate 10 --duration 3
$ grep -a "^ /cmd_vel" bridge/windows/build/ros2_bridge_daemon.log | tail -1
  ^ /cmd_vel                   geometry_msgs/msg/Twist            msgs=19       failed=0
$ bash bridge/linux/ros2_bridge.sh stop; bash bridge/linux/ros2_bridge.sh pid; echo "pid rc=$?"
[ros2_bridge] stopping daemon (pid 1652093)
pid rc=1
```

七条话题和类型与 Windows 侧一模一样，上行计数器 `msgs=19` 说明**本机发布的 `/cmd_vel` 真的到了
agent**，`stop` 之后 `pid` 返回 1、ready 文件也删掉了。跑的是同一份 `ros2_shim` + `bridge_daemon`
二进制（Linux 侧第一次全量编出来的那棵），不是移植品。

### 四、两个必须记下来的边界

* **发现不看本机**。`gRPC/ros2_gRPC_sdk/src/discovery/discovery.cpp:461-486` 明确把本机所有地址
  （所有网卡，含 loopback）排除在扫描之外。于是「守护进程和 agent 在同一台机器上」——恰恰是 Linux
  侧最常见的用法——**局域网扫描永远找不到它**，日志会一直 `probing 253/253 hosts` +
  `retrying in 2000 ms`。给地址就行：`ROS2_BRIDGE_AGENT=127.0.0.1:50051`。跨机（Windows ↔ Ubuntu）
  不受影响，这也是它在 Windows 上从来没暴露过的原因。
* **`topic pub --once` 可能赶不上上行**。一次性发布（进程立刻退出）在上行订阅挂上之前就结束了，
  计数器停在 `msgs=0`；换成 `--rate 10 --duration 3` 立刻就是 19 条。这一条与平台无关，验收脚本
  一直用的是后者。

### 五、顺带把它变成门禁

`tools/check_cross_platform.py` 的推送清单加了 `bridge/linux`，表里多一行
`bridge: control entry point`：本地跑 `ros2_bridge.bat status`，远端跑**整条生命周期**（已经在跑就
放过，否则 `start` → `pid` → `stop` → `pid` 必须失败）。先判断「已经在跑就放过」是为了不让门禁在
共享机器上把别人正在用的守护进程杀掉。

## 十五、三套老入口的本机路径（2026-09-17 深夜）

`nav2\ros2_shim` 的 `build.ps1` / `run_tests.ps1` / `generate.ps1` 不再各自回答「MSYS2 在哪、用哪几个
核、python 在哪」，一律转发给 `build.py` / `tools\run_ctest.py` / `tools\generate_messages.py`；顺带
清掉了手动工具与 `bridge_env.bat` 里的同类硬编码。改动与实测见 `05-audit.md` 第十一节。

仍然**故意留着**的绝对路径，都是给人抄的，不是给程序读的：

* `README.md` / `bridge\README.md` / `bridge\docs\02-running.md` 里点源 `ros2.ps1` 的示例；
* `bridge\windows\ros2.ps1` 与 `ros2_shell.bat` 注释里的同一条路径；
* `bridge_env.bat` 里本机 MSYS2 的默认值（`ROS2_BRIDGE_MSYS2` / `CPU_MSYS2_ROOT` 都能盖掉）。

它们目前与实际位置一致，仓库换地方就会过期。之所以没改成相对写法：「点源哪一份文件」这一行要能被
直接粘进一个和当前目录无关的窗口，相对路径做不到这一点 —— 这是个取舍，选了不动。

## 十六、图注册表的**跨进程**丢写：关上了（2026-09-17 深夜）

第七节第 2 条留到最后的那一半 —— **别的进程**看到的图 —— 这一轮补上了。它和第十节、第十三节那些
改动落在同一个文件上（`nav2\ros2_shim\include\ros2_shim\graph.hpp`），所以改动与证据一起记在这里。

**缺陷的形状。** 每次变更都要重写自己的 `<pid>.graph`，写和换是两步：先落一份 `.tmp`，再让
`install_file()`（`MoveFileExW` → `ReplaceFileW` → `MoveFileExW`，每轮 50 ms 预算）把它换到位。
Windows 上如果某个进程把这份文件按住、又不给 delete 共享权限，三种换法**全部失败**，而
`flush_locked()` **丢掉了这个返回值**：文件停在上一版内容，进程内存里却是新的。本进程自己的查询早在
第九节就改从内存取了，别人没有内存可看 —— 这就是「服务在跑、别人却报
`get_state service is not available!`」和「发布者门控算出 0 个订阅者」的来源。

**为什么不能只加一次重试。** `install_file()` 上面的注释已经写明：`ReplaceFileW` 失败时 `.tmp`
**可能已经被它带走**，所以「把同一个名字再交给 `install_file()` 一次」不成立 —— 重试必须是**整次
写入**（重新写一份 `.tmp` 再换）。

**改法。** `flush_locked()` 现在检查返回值，失败就置 `dirty_` 并叫醒一个重刷线程
（`ProcessRegistry::reflush_loop()`）；线程每轮**重新写 `.tmp` 再换**，轮间隔 10 ms（一次失败的
`install_file` 本身已经花掉 50 ms 预算）。三个方向都有讲究：

* 失败**不再由调用者等** —— 调用者是正在注册东西的节点，为一个可能按住一秒的读者停在那里不是它的事；
* 线程只在 `dirty_` 时醒来，落盘成功就回到条件变量上，所以「第一次就成功」这条常见路径**不花任何
  额外代价**；
* 析构**先停线程、再删自己的 `.graph`**：顺序反过来的话，线程的一次重刷会把文件放回去，给一个已经
  退出的进程留下图条目。

**回归：`ros2_shim_graph_rewrite_cross_process`（32 条里的第 32 条）。** 它是**真的跨进程** —— 把自己
再起一次（Windows `CreateProcessW` / POSIX `fork`+`execl`）当持有者，用不带 `FILE_SHARE_DELETE` 的
句柄把 `<pid>.graph` 按住 800 ms，父进程在这段时间里连写 5 条记录，然后**什么都不再做**：

| 断言 | 现场 |
| --- | --- |
| 持有者真的把文件按住了（子进程写 marker 报到） | PASS |
| 按住期间那次交换确实**没**落地（Windows；POSIX 的 rename 不会被读者拒绝，跳过这一条） | PASS |
| 放手后它**自己**落地，父进程没有再改任何东西 | PASS |
| 5 条记录一条不缺、文件带 `#end`、最初那条还在 | PASS（0 missing） |
| 整条用例 | **0.95 s**（串行 32/32 那次）；单独重跑 10 轮 **10/10 干净**，0.88~1.02 s |

**反向验证（这条决定它算不算数）：** 把 `flush_locked()` 改回原来的形状（`install_file()` 的返回值
照样丢掉、不置 `dirty_`）重新编译，同一个 ctest 用例 **rc=8、10.88 s** —— 卡满 10 s 的等待窗口，
`all 5 records written while it was held are in the file (5 missing)`。随后按 sha256 逐字节还原
（`f193cff6…`）并重新构建，变回 rc=0。

**这一轮同一棵树的现场：**

| 平台 / 树 | 现场 |
| --- | --- |
| Windows · 兼容层 + 魔改 nav2（串行） | `python tools\run_ctest.py shim`：**32/32、52.27 s**（再跑一次 52.39 s） |
| Windows · `ctest -j 8` | 4 轮 **全绿**（21.4 / 19.3 / 14.9 / 19.1 s，每轮 32 条） |
| Windows · `bridge\windows`（也 include 这份头） | 重编后 **4/4、2.35 s** |
| **Linux**（`ubuntu-ASUS`，8 核） | 把改动的四个文件推过去：`python3 nav2/ros2_shim/build.py` **410/410、rc=0**；`ctest --test-dir nav2/ros2_shim/build` **32/32、15.09 s** —— 新用例的 POSIX 分支（`fork`+`execl`、`::open`）在同一次里编过也跑过，并按设计跳过「读者拒绝 rename」那一条 |

守护进程停掉再重编、跑完再起回来，状态仍是 7 条远程话题。

**顺手修掉的同类残留（%TEMP%）：** 这个新用例第一版把 marker 写在一个**没人创建过**的目录里 ——
它自己拼了 `graph_dir()` 的规则，而忘了 ctest 会给每条用例设 `NAV2_SHM_NAMESPACE`（图目录带后缀）。
在 ctest 下表现成「持有者从来没起来」，单独跑却是绿的。现在两条图用例都改成**问注册表**要目录
（`file.parent_path()`），并且跑完删掉自己那一份。实测：`%TEMP%` 里
`ros2_shim_graph_rewrite_test_*` 的残留 **3 → 0**（另外两个是更早的 `.scratch` 脚本留下的，已清）。

**这条证据不能证明：** 读者在文件被按住的那 800 ms 里仍然读到上一版 —— 这是「文件即通道」的固有
性质。能保证的是丢写**不会永远**留在上一版。Windows 侧 `-j 8` 只跑了 4 轮、新用例自己 10 轮，
不是 `-j 8` × 10 轮那个量级的压测；Linux 侧跑的是串行 32 条。

## 十七、slam_toolbox 的服务表补全（2026-09-17 深夜）

第十六节合掉的是「图文件丢写」；这一轮合掉的是第七、八节里那条**功能缺口**：`slam_toolbox` 的
11 个 srv 里只有 2 个在跨机编解码表里（`SerializePoseGraph` / `DeserializePoseGraph`），
而移植过来的节点实际 `create_service` 了 **13 个**服务，其中 9 个的类型**表里没有** ——
`ros2 service call slam_toolbox/reset ...` 这类调用被 `no codec for service type` 挡在门口。

### 一、缺口是 9 条类型，不是 9 个名字

对着 `nav2/third_party/slam_toolbox` 数了一遍 `create_service<>`（13 处），再和
`nav2/ros2_shim/generated/include/slam_toolbox/srv/`（11 个生成头，早就都在）对齐：

| 服务名 | 类型 | 之前 | 现在 |
| --- | --- | --- | --- |
| `slam_toolbox/clear_changes` | `slam_toolbox/srv/Clear` | ❌ | ✅ |
| `slam_toolbox/manual_loop_closure` | `slam_toolbox/srv/LoopClosure` | ❌ | ✅ |
| `slam_toolbox/toggle_interactive_mode` | `slam_toolbox/srv/ToggleInteractive` | ❌ | ✅ |
| `slam_toolbox/save_map` | `slam_toolbox/srv/SaveMap` | ❌ | ✅ |
| `slam_toolbox/merge_submaps` | `slam_toolbox/srv/MergeMaps` | ❌ | ✅ |
| `slam_toolbox/add_submap` | `slam_toolbox/srv/AddSubmap` | ❌ | ✅ |
| `slam_toolbox/pause_new_measurements` | `slam_toolbox/srv/Pause` | ❌ | ✅ |
| `slam_toolbox/reset` | `slam_toolbox/srv/Reset` | ❌ | ✅ |
| `slam_toolbox/clear_queue` | `slam_toolbox/srv/ClearQueue` | ❌ | ✅ |
| `slam_toolbox/serialize_map` | `slam_toolbox/srv/SerializePoseGraph` | ✅ | ✅ |
| `slam_toolbox/deserialize_map` | `slam_toolbox/srv/DeserializePoseGraph` | ✅ | ✅ |
| `slam_toolbox/dynamic_map` | `nav_msgs/srv/GetMap`（上游类型，不在 slam_toolbox/srv 里） | ❌ | ✅ |
| `slam_toolbox/clear_localization_buffer` | `std_srvs/srv/Empty` | ✅ | ✅ |

`dynamic_map` 是唯一用 nav_msgs 类型的那一个（`slam_toolbox_common.cpp:451`），它答的是一整张
`OccupancyGrid`，所以 `nav_msgs/srv/GetMap` 的 codec 也一起补了 —— 那个响应里的嵌套消息带序列，
是全表唯一一个「嵌套消息里有数组」的 service 载荷。

### 二、改法与代价

三个位置各加一段（`bridge/windows/src/service_registry.cpp`）：

* `describe()`：空请求 / 空响应的分支多了 12 个类型（Clear / LoopClosure / MergeMaps /
  ToggleInteractive 的收发，加 Pause / ClearQueue 的请求、AddSubmap 的响应、`GetMap` 的请求）；
  `AddSubmap` 的请求和 `SerializePoseGraph` 共用「打一行 filename」；另外补了
  `Reset` / `SaveMap` 的 result 码（把 `RESULT_NO_MAP_RECEIEVD` 这个**上游拼错的常量名**原样
  翻成 `no map received`）、`ClearQueue` / `Pause` 的 `status`，以及 `GetMap` 响应的图摘要
  （frame / 分辨率 / 尺寸 / 原点 / 格子数）。
* `applyRequestFields()`：`AddSubmap.filename`、`Reset.pause_new_measurements`，以及
  `SaveMap` 的 `name.data` —— 那是**全表唯一一个字段藏在另一个消息里**的请求（`std_msgs/String`），
  所以它的命令行写法是 `name.data=...`。
* `registry()`：9 个 slam_toolbox 类型 + `nav_msgs/srv/GetMap`，表从 11 条变成 **21 条**
  （`ros2 interface list` 从 58 变成 68）。

### 三、这一轮的现场

| 平台 / 树 | 现场 |
| --- | --- |
| Windows · `bridge\windows` 重编 | `python bridge\scripts\build_bridge.py`：8 个目标全过（改动只有一个编译单元） |
| Windows · `bridge\windows` ctest | **4/4、2.79 s**（`bridge_service_roundtrip` 1.09 s） |
| Windows · 用例本身 | **156 checks / 0 failure**；断言 `supported_services().size() == 21`（11 → 21） |
| **Linux**（`ubuntu-ASUS`，8 核） | 两个改动文件 scp 过去、同一套 `python3 bridge/scripts/build_bridge.py` + `ctest --test-dir bridge/windows/build`：**4/4、2.07 s**；直接跑用例同样是 **156 checks / 21 条** |

活节点（本机移植的 `slam_bridge_node.exe`，`/scan` 由 Ubuntu 的仿真经 gRPC 桥镜像进共享内存）
上真打了七条，全部 rc=0、全部走共享内存通道、**不经过守护进程**：

```text
$ ros2 service call slam_toolbox/reset slam_toolbox/srv/Reset pause_new_measurements=false
request:
pause_new_measurements: false
response:
result: 0 (success)

$ ros2 service call slam_toolbox/clear_changes slam_toolbox/srv/Clear
response:
(empty)

$ ros2 service call slam_toolbox/pause_new_measurements slam_toolbox/srv/Pause
response:
status: true

$ ros2 service call slam_toolbox/clear_queue slam_toolbox/srv/ClearQueue
response:
status: true

$ ros2 service call slam_toolbox/toggle_interactive_mode slam_toolbox/srv/ToggleInteractive
response:
(empty)

$ ros2 service call slam_toolbox/manual_loop_closure slam_toolbox/srv/LoopClosure
response:
(empty)

$ ros2 service call slam_toolbox/dynamic_map nav_msgs/srv/GetMap
response:
map:
  frame_id: "map"
  resolution: 0.05
  size: 472 x 472
  origin: (-11.7, -11.8)
  data: 222784 cell(s)
```

`dynamic_map` 那张 472x472 的图是节点**当场解出来的**，所以这一条同时证明三件事：类型表里有它、
请求真的到了节点、响应（嵌套 `OccupancyGrid`）真的解得开。

剩下两条类型（`AddSubmap` / `MergeMaps`）由 `merge_maps_kinematic.exe` 提供 —— 那个 exe
**早就编出来了**（`nav2\ros2_shim\build\slam_toolbox_port\` 里，和 `sync_` / `async_` /
`localization_slam_toolbox_node.exe` 一起），只是以前没起过。把它拉起来后也是两条 rc=0：

```text
$ ros2 service list -t | grep merge
/slam_toolbox/add_submap [slam_toolbox/srv/AddSubmap]
/slam_toolbox/merge_submaps [slam_toolbox/srv/MergeMaps]

$ ros2 service call slam_toolbox/add_submap slam_toolbox/srv/AddSubmap filename=C:/maps/nope.posegraph
request:
filename: "C:/maps/nope.posegraph"
response:
(empty)

$ ros2 service call slam_toolbox/merge_submaps slam_toolbox/srv/MergeMaps
request:
(empty)
response:
(empty)
```

`add_submap` 那条故意传了一个不存在的文件：节点读不到就在日志里报
`Failed to read file: C:/maps/nope.posegraph` 然后返回空响应（上游的 `return true` 就是这个语义）。
它验的是**通路**（filename 原样到了节点），不是「子图真的加载了」；`merge_submaps` 在零子图状态下
也只是一趟空跑。**九条新类型全部在活节点上被调到。**

### 四、剩下的是 `save_map` 的 `system()`，不是类型表

`save_map` 的类型通道是通的（请求里的 `name.data` 原样到了节点），但节点自身的实现是上游那句
`system("ros2 run nav2_map_server map_saver_cli -f <name> ...")`
（`third_party/slam_toolbox/src/map_saver.cpp:85`）：本机没有 `ros2`、也没有 `ros2 run`，
子进程必然非零，于是响应是 `result: 255 (undefined failure)`。这是**上面已经记过的已知缺口**
（「`map_saver` 的 `system()` 路径」那一行），绕法不变：
`nav2\ros2_shim\build\map_saver_cli.exe -t /map -f <name> --fmt pgm`。

**这条证据不能证明**：这些服务的**语义**都对 —— `add_submap` / `merge_submaps` 是空跑，
`save_map` 内部失败（上面第四节），`clear_changes` / `toggle_interactive_mode` /
`manual_loop_closure` 改完状态也没去核对 SLAM 内部。这一轮验的是**通路**：类型表里有它、
请求到得了节点、响应解得开。也不证明 `localization_slam_toolbox_node.exe` 那条
`clear_localization_buffer` —— 它用的 `std_srvs/srv/Empty` 本来就在表里，没重跑。


## 十八、nav2 自己的九个 srv：costmap / planner 的服务表也补齐了（2026-09-17 下午）

第十七节补的是 `slam_toolbox` 的服务；这一轮补的是 **nav2 本体**的。本机一条活着的 nav2 栈
（`nav2_stack.bat`）里，`amcl` / `planner_server` / `controller_server`（带两个 costmap）/
`behavior_server` / `smoother_server` / `waypoint_follower` 一共提供 **12 个 `nav2_msgs` 类型的服务**，
而表里 `nav2_msgs` 只有两条（`Toggle` / `SetInitialPose`）—— costmap 的四条清理、取图、取值、
planner 的 `is_path_valid`、map_server 的存取，一个都过不来。

### 一、补的是 9 条类型

| 服务（活栈上的名字） | 类型 | 之前 | 现在 |
| --- | --- | --- | --- |
| `/global_costmap/clear_entirely_global_costmap`（`local` 同名一族） | `nav2_msgs/srv/ClearEntireCostmap` | ❌ | ✅ |
| `/global_costmap/clear_around_global_costmap` 一族 | `nav2_msgs/srv/ClearCostmapAroundRobot` | ❌ | ✅ |
| `/global_costmap/clear_except_global_costmap` 一族 | `nav2_msgs/srv/ClearCostmapExceptRegion` | ❌ | ✅ |
| `/global_costmap/clear_around_pose_global_costmap` 一族 | `nav2_msgs/srv/ClearCostmapAroundPose` | ❌ | ✅ |
| `/global_costmap/get_costmap`（`get_static_layer` / `get_obstacle_layer` 共用这个类型） | `nav2_msgs/srv/GetCostmap` | ❌ | ✅ |
| `/global_costmap/get_cost_global_costmap` 一族 | `nav2_msgs/srv/GetCosts` | ❌ | ✅ |
| `/is_path_valid` | `nav2_msgs/srv/IsPathValid` | ❌ | ✅ |
| `map_server/load_map` | `nav2_msgs/srv/LoadMap` | ❌ | ✅ |
| `map_server/save_map` | `nav2_msgs/srv/SaveMap` | ❌ | ✅ |

`nav2_msgs` 那 20 个 srv 因此从认 3 个变成 **认 12 个**；表 21 → 30，`ros2 interface list` 68 → 77。
剩下 8 个（`AddExclusionZone` / `RemoveExclusionZone` / `AddShapes` / `RemoveShapes` / `GetShapes` /
`DynamicEdges` / `SetRouteGraph` / `ReloadDockDatabase`）分别由 `collision_monitor`、两个 object
server、`route_server`、`docking_server` 提供，同一套模式可以继续补。

### 二、改法：四个 helper 覆盖了这次和上次的一多半

三段式照旧（include / `describe()` / `applyRequestFields()` / `registry()`），但这一次共用得多：
四条清理服务的请求是**同构**的（`reset_distance` + `plugins`，`ClearCostmapAroundPose` 多一个
`PoseStamped`），`GetCostmap` 的响应就是「一张 `OccupancyGrid` + 一个 `layer`」——
于是新加的 `describeGrid()` / `describeCostmap()` / `printPlugins()` / `describePoseStamped()`
四个 helper 一次覆盖 `GetCostmap`、`GetCosts`、四条清理，以及上一轮手写的
`nav_msgs/srv/GetMap` 响应（那一段这一轮改成调 `describeGrid()`）。
`applyRequestFields()` 里新加 `#define ASSIGN_SEQ(EXPR)`（照 `type_registry.cpp` 的写法抄）
来塞 `plugins` 与 `GetCosts` 的 `poses[]` 这两个序列。

### 三、现场

九条类型里的**七条**已经在活着的 nav2 节点上真调过：`clear_entirely_*`（global / local）、
`clear_around_*`、`clear_except_*`、`get_cost_*` 都回 `success: true`，`get_costmap` 回一张
**480 x 480 @ 0.05 的 `master` 图层**，`is_path_valid` 对空路径回 `invalid_pose_indices: [0]`。
逐条输出，加上「`/is_path_valid` 不带 `/planner_server` 前缀」和「detached 启动 `nav2_stack.bat`
会假死」这两个坑，都记在 `03-validation.md` 证据 52。**没有**在活节点上调过的是 `LoadMap` /
`SaveMap`（它们要 `map_server.exe`，那一轮没起）。


## 十九、nav2_msgs 的 srv 认到 20/20（2026-09-17 下午）

第十八节把 nav2 的 costmap / planner / map_server 九条补上之后，`nav2_msgs` 那 20 个 srv 还剩 8 条
没有 codec。这一轮把它们补完：表 30 → **38**，`ros2 interface list` 77 → **85**，
**`nav2_msgs` 的 srv 从此是 20/20**。

### 一、补的是哪八条

| 类型 | 活节点上的服务名 | 谁提供 |
| --- | --- | --- |
| `nav2_msgs/srv/AddExclusionZone` | `/collision_monitor/scan/add_exclusion_zone` | `collision_monitor.exe`（每个 source 一份） |
| `nav2_msgs/srv/RemoveExclusionZone` | `/collision_monitor/scan/remove_exclusion_zone` | 同上 |
| `nav2_msgs/srv/AddShapes` | `/vector_object_server/add_shapes` | `vector_object_server.exe` |
| `nav2_msgs/srv/RemoveShapes` | `/vector_object_server/remove_shapes` | 同上 |
| `nav2_msgs/srv/GetShapes` | `/vector_object_server/get_shapes` | 同上 |
| `nav2_msgs/srv/DynamicEdges` | `/route_server/<scorer>/adjust_edges` | `route_server.exe` + `DynamicEdgesScorer` 插件 |
| `nav2_msgs/srv/SetRouteGraph` | `/route_server/set_route_graph` | `route_server.exe` |
| `nav2_msgs/srv/ReloadDockDatabase` | `/docking_server/reload_database` | `docking_server.exe` |

这八条是这张表里**最深**的一批载荷：`AddExclusionZone` 的请求是一条消息，消息里还有一个
`Point32[]`；`AddShapes` / `GetShapes` 收发都是两个消息列表；`CircleObject` / `PolygonObject` /
`RemoveShapes` 里带着 `unique_identifier_msgs/UUID`（16 字节，按 32 位十六进制打）；
`DynamicEdges` 的 `adjust_edges` 又是一个 `EdgeCost` 列表。可写的部分只有标量、字符串和
`uint16[]`（`ASSIGN_SEQ`），消息数组一律「命令行没有拼法」，和 `GetCosts.poses` 同一条规矩。

### 二、现场

活节点上真调了**七条**（第八条 `DynamicEdges` 的插件这一次没被实例化，见证据 53 第四节）：
`collision_monitor` 上把一个圆形禁区加进去、按名字删掉、再全删（三次都回 `success: true`，且
`message` 是节点自己写的 `Added zone 'keepout_a' to source 'scan'`）；
`vector_object_server` 上 `get_shapes` → `add_shapes` → `remove_shapes all_objects=true` →
`get_shapes`，最后一个回答从「一个圆 + 一个多边形」变成空 —— **能看出状态真的变了**；
`route_server` 上 `set_route_graph` 先给不存在的文件（`success: false`），再给树里那张
`sample_graph.geojson`（`success: true`）；`docking_server` 上 `reload_database` 给不存在的文件
回 `success: false`。逐条输出在 `03-validation.md` 证据 53。

### 三、还差什么

- `DynamicEdges` 没有活节点证据：它的服务由 `DynamicEdgesScorer` 插件在 `configure()` 里
  `create_service`，而这一次运行里那条插件没被实例化（即使图已经成功加载）。
- 这八条全是**本机节点自己提供**的服务，走共享内存直达，**不经过守护进程** —— 和第十七 / 十八节
  同一性质。
- 跨机方向仍然是那堵墙：`ros2 interface list` 85 / 319，剩下的是话题（38/216）与 action（9/20）。

## 二十、上行默认表补上 nav2 的两条，`%TEMP%` 图目录开始自清（2026-09-17 傍晚）

这一轮改的都是**已经认得出来的东西**，没有一条新类型：

1. **上行白名单**（第四节末、第八节第 2 条）：默认表从 3 个名字变成 **5 个** —— 多了 nav2 回话的
   `/plan` 与 `/behavior_tree_log`。`ros2.bat` / `ros2_bridge.bat` **一行没改**，因为默认值长在
   守护进程里。挑这两条而不是两张 costmap 的理由是「事件驱动 vs 定时」：路径算出来、行为树分支
   走完才发一条，闲置不花钱；costmap 定时重发几百 KB，留在 `--uplink` 里按需开。
   实测（证据 54）：全栈跑一次，守护进程 `uplink=5`、`/plan msgs=16`、`/behavior_tree_log msgs=15`、
   全部 `failed=0`；机器人侧把 `/plan` 逐字段解出来。**验收第 3 条从此不需要任何参数。**
   当天晚上又补上后半句：**验收现在会断言这一条** —— `check_uplink_nav2` 要求默认表把两条 nav2
   名字武装起来、并要求 `/plan` 真的转发成功（证据 56）。
2. **`%TEMP%` 残留**（第八节、第十一节表末）：清掉 56 个目录（其中 54 个 0 字节）。根因不是
   `acceptance_check.py`（它早就 `mkdtemp` + `rmtree` 了），而是**图注册表**：`NAV2_SHM_NAMESPACE`
   一设，图目录就多一个后缀，进程退出只删自己那个 `<pid>.graph`、目录留着。两个关闭点都在
   `nav2\ros2_shim\include\ros2_shim\graph.hpp`，判据是同一句「`std::filesystem::remove`
   对目录只在空的时候成功」：`~ProcessRegistry()` 末尾（正常退出）、`collect()` 扫完僵尸之后
   （被 kill 的进程）。重编 + 整棵 shim 测试树 **32/32**，跑完目录数 **3 → 3**
   —— 而这一轮之前同一次运行会留下 14 个（证据 55）。
3. **仓库成为 git 仓库**（F 节）：`git init -b main` + `git add -A`（4498 个条目，**没有提交**）。
   新增 `.gitattributes`（`* text=auto eol=lf`，`.bat` / `.ps1` 是 CRLF），因为 `tools\check_docs.py`
   对 `.md` 里的任何一个 CR 字节都报错 —— 一次 `core.autocrlf=true` 的检出就能把整个文档树弄红。

## 二十一、假对端不够像真对端：跨机的空请求服务一条都没通过（2026-09-17 深夜）

这一节记的不是「新功能缺什么」，而是**已有的功能有一半从来没通过** —— 事后看，
它属于 D 节那条「对端不是真 ROS2」的利息，只是这一次坏在仿真对端身上。

### 一、缺的是什么

`bridge\scripts\acceptance_check.py` 跑完 8/8，结尾一行 `[warn] /sim/reset exited 1`。
顺着它查下去：**凡是请求消息没有字段的服务，从 Windows 侧一条都调不通。**
`/sim/reset`、`/sim/status`（`std_srvs/srv/Trigger`）、`/sim/pause`、`/sim/resume`
（`std_srvs/srv/Empty`）全部 5 s 超时，而 `/sim/set_paused`（`SetBool`，请求里有一个 `bool`）
一直是好的。根因是仿真对端的 CDR 解码器要求「读完正好到底」，而桥发出去的 8 字节
（封装头 + 补到 4 字节的体，证据 57 说真 ROS2 就是这么发的）被它读成「4 trailing byte(s)」。

完整证据在 `03-validation.md` 证据 59。

### 二、为什么它能藏住

两件事叠在一起，缺了任何一件都藏不住：

1. **对端只跟自己比。** 仿真对端自己的编码器产 4 字节，`tests/test_services.py` 还把这个自洽
   写成了断言（`assert len(empty) == 4, "an empty request is just the CDR header"`），
   于是仿真测试全绿，而桥真正发出去的那 8 个字节从来没有进过任何一条用例。
2. **验收把唯一的调用点扔了。** `/sim/reset` 在 `acceptance_check.py` 里是收尾杂事，
   退出码直接被丢掉，只打一行 `[warn]`。8/8 的「8」里不含它。

也就是说：**「验收全绿」这件事本身曾经是个弱信号。** 现在它是第 9 条真检查，断言的是效果
（回应 `success: true` + `/odom` 回到原点 + 没有命令在推车轮），不是退出码。

### 三、修完之后仍然开着的

* **Windows 侧的编码器只补「空体」，不补 1–3 字节的体**（证据 57 第五节记的待办，仍然开着）：
  `std_msgs/msg/Bool` 编出来是 5 字节，rclpy 是 8 字节。真 ROS2 读 5 字节是 ok 的，所以没有观察到的
  后果；要关它得连 `bridge\tests\fixtures`（`bool.cdr` 就是 5 字节）一起重造。仿真对端的编码器这一轮
  **已经按完整规则改了**，两边在这一点上现在是不一致的。
* **这一轮没有在真 ROS2 的服务端上量空请求服务。** 量的是仿真对端。
* **同一类风险还在别处**：仿真对端里任何「只和自己往返」的路径（编码器 ↔ 自己那个解码器）都
  可能和真线长的样子不一致，而验收用的是仿真。这一轮只撞出了 CDR 空体这一处；
  「用真对端跑一遍验收」仍然是 D 节那条缺口。

## 二十二、复核之后仍然开着的（2026-09-17 晚）

这一节是证据 60 的缺口面。清单上那十条已经全部落地（逐条见证据 60 第四节），下面是
**复核之后仍然开着**的，按「现在挡着什么」排。

### 一、CI 有文件、没跑过，而且少了一半门禁

【查】`.github/workflows/cross-platform.yml` 在，两个 job（windows / linux）都走仓库自己的
入口（`nav2/ros2_shim/build.py`、`bridge/scripts/build_bridge.py`、`tools/run_ctest.py`）——
这正是它存在的理由：写死的 MSYS2 路径或 `".exe"` 拼法会在另一台上当场失败。

但三件事同时成立：

1. **它一次都没执行过。** 仓库 0 commit、没有远端，`on: push / pull_request` 从来没被触发。
   「两边都能编」今天仍然是**手工**跑 `tools/check_cross_platform.py` 的结果，不是门禁 ——
   2026-09-19 两侧各跑一遍、六步全绿（明细见证据 75），但手工绿一次不等于下一条提交会绿。
2. **它跑四棵树，不是五棵。** `tools/run_ctest.py` 的 docstring 写着 five test trees，workflow
   里只有 shim / shm / bridge / grpc —— `sim`（仿真的 pytest 树，71 条）不在里面。
3. **仓库自己的三个检查一个都没进 CI**：`tools/check_docs.py`（`.md` 的 BOM / LF / 围栏）、
   `tools/find_scratch_refs.py --check`（`.scratch` 的引用还指不指得到东西）、
   `nav2/patches/verify_patch.py`（那个 `FATAL_ERROR` 门控的「人读版本」）。这三个既不需要
   编译器也不需要机器人，是**最便宜**的一类门禁，现在却只靠人记得跑。

代价是具体的：证据 60 第五节那个 `fsedit.py` 缺陷让**每一次 `.md` 编辑都留一条警告**，
而没有任何自动化的东西会因此变红，所以它活到今天。

`tools/check_cross_platform.py` 本身**不适合**进 CI：`--remote` 要 SSH 到 192.168.1.113，
`--local` 和 workflow 里那些入口重复。这一条写清楚比硬塞进去好。

### 二、Windows 侧的编码器仍然只补「空体」

【查】证据 57 第五节记的待办仍然开着：`std_msgs/msg/Bool` 编出来 **5 字节**，rclpy 是 **8**。
仿真对端这一轮**已经改成完整规则**（体不足 4 字节补到 4，证据 59），所以**两边现在不一致**。
线上真正会走到这条的只有 `Toggle` / `SetBool` 的请求，而真 ROS2 读 5 字节是 ok 的
（证据 57 第二节的表），因此没有观察到的后果。要关它得连 `bridge\tests\fixtures`
（`bool.cdr` 就是 5 字节）一起重造，单独排一轮。

### 三、编解码表的长尾

【查】`bridge\windows\src` 三张表：话题 **40**、service **38**、action **20**（action 那一栏
2026-09-17 深夜补满，第二十三节 / 证据 61；话题那一栏当晚从 38 涨到 40 —— `Image` /
`CameraInfo`，证据 62）。
【跑】`ros2.bat interface list` = **99** 个类型（41 msg + 38 srv + 20 action）；真 ROS2 Jazzy 上
`ros2 interface list` 是 **258** 行。具体缺的是：

* ~~**`sensor_msgs/msg/Image` 不在表里** → 相机类话题没有任何通路~~ —— **2026-09-17 深夜
  关掉了**（证据 62：`Image` + `CameraInfo` 一起入表，`interface list` 96 → 98，37 个 fixture
  双向逐字节相等）。相机一族**只补了这两个**：`CompressedImage`、`PointCloud` 与其余传感器
  变体仍在表外；
* **41 个话题 codec 里有 3 个没有 fixture**：`visualization_msgs/msg/MarkerArray` 与
  `std_msgs/msg/Empty` 是**写了理由的**（前者 rosbags 的 Jazzy 类型表把 `UVCoordinate` 拼成
  `UvCoordinate`，重建不出来；后者是 rclpy 的 `structure_needs_at_least_one_member` 让空体
  两边不一样长），`nav2_msgs/msg/Costmap` 是**唯一没写理由的** —— 它只在 service 那一侧被
  `GetCostmap` 的打印器覆盖，没有逐字节对照（第四个 ~~`action_msgs/msg/GoalStatusArray`~~
  **2026-09-19 补上了**，证据 73：`goal_status_array` cdr=61 / shim=54，`bridge_cdr_check`
  37 → **38 个 fixture**、111 → **114 组字节比较**）；
* 带数组字段的 goal（`SmoothPath` / `FollowWaypoints` / `NavigateThroughPoses`）没有命令行拼法
  —— 本机用 `rclcpp_action::Client` 走同一份 codec 是通的，发不出去的是 `send_goal` 那一条命令行；
* `/fibonacci` 这类 action 仍然被 `skipping ... no codec for action type` **静默跳过**：它不在
  nav2 里，补不补是个取舍。

每一条都是「加一个表项 + 一组 fixture」的体力活，不是设计问题。

### 四、对端真实性与验收底座

【跑】这一轮所有跨机数字都是和**仿真对端**（50051）之间量的；真 ROS2 那台（50111）当时不在线，
Ubuntu 上连 tmux 会话都没了。所以「用真对端跑一遍验收」仍然没做，验收仍然建在假对端上。
这不是新缺口（D 节就有），但这一轮又量了一次它的形状：**假对端里任何「只和自己往返」的路径
都可能和真线长的样子不一致，而验收看不出来** —— 证据 59 撞出的 CDR 空体就是一处。

### 五、QoS 只有声明，没有行为

【查】`nav2/ros2_shim/include/rclcpp/qos.hpp` 提供 `ReliabilityPolicy` / `DurabilityPolicy` /
`HistoryPolicy` 的**声明**，底层是丢最旧的有损环（实测最多丢 48 976 / 50 000 条）。没有 DDS 的
不匹配行为（一个 BEST_EFFORT 的读者不会让 RELIABLE 的写者改变行为），枚举名还是 RMW 的老拼法
（`RELIABLE` 而不是 `Reliable`）—— 照新拼法写的代码拿到这个 shim 上是**编译错误**。
nav2 1.5.1 自己不用这些拼法，所以它今天不挡任何东西，属于「潜在缺口」。

### 六、工程化的其余几条

【查】0 commit；**没有 LICENSE**；没有英文文档；端到端时延没有基线；断线重连 / 故障注入
**0 条用例**。另外仓库里 `add -A` 过的 **4513 个条目 / 195.7 MB**，其中 >2 MB 的 **11 个文件
占 146 MB**（38 MB 的 `mapping_steves_apartment.gif`、25.7 MB 的 `test3.bag`、
`nav2/nav2_extracted` 里四个共 54.5 MB 的 `demo.gif`）。`.gitignore` 已经排掉 zip 与
`third_party/dl/`，但**解包后的同一份内容还在树里**。真要开第一次 commit，这一条得先决定。

### 七、其余已知缺口没有变化

* **nav2 上游还差 7 个包**（第七节 A/E）；
* **`rosbag2` / `rclpy` 全缺**（第七节）；
* **SLAM**：`merge_maps` 的合并语义没验、`map_saver` 的 `system()` 路径在 Windows 必失败、
  跨机存档（Ubuntu 建图 → Windows 定位）没做（第七节）；
* **「有二进制、没证据」**：`DynamicEdges`、MPPI 的备用 motion model 与 3 个 critic、
  `assisted_teleop` / `drive_on_heading`（`navigate_through_poses` 的树已由证据 68 关掉）。

### 八、按「现在挡着什么」排的建议顺序

1. **把 CI 补全并让它跑起来**（第一节）—— 它是唯一一条「补完之后能防止别的缺陷再溜进来」的；
2. ~~`action` 表的 9 条（第三节）~~ —— **2026-09-17 深夜关掉了**（第二十三节 / 证据 61：
   19 个 nav2 action 全在，`interface list` 85 → 96，`bridge` 树 4/4）；
3. ~~`sensor_msgs/Image`（第三节）~~ —— **2026-09-17 深夜关掉了**（连同成对的 `CameraInfo`，
   第二十四节 / 证据 62：`interface list` 96 → 98，37 个 fixture 双向逐字节相等）；
4. Windows 侧 1–3 字节短体的补零（第二节）—— 关掉「两边不一致」这个状态；
5. 真对端跑一遍验收（第四节）—— 把验收底座从假对端上挪开。


## 二十三、action 表 9 → 20 关掉之后：撞到一个 639 s 的挂死（2026-09-17 夜）

第二十二节排的第二条（`action` 表）关掉了，过程与实测在 `03-validation.md` 证据 61。这一节
记三件事：**关掉的**、**路上撞到的一个 639 s 挂死**、**还开着的一条**。

### 一、nav2 的 19 个 action 全在表里了

【查】上游 `nav2_msgs/action/*.action` 一共 **19** 个，`nav2/ros2_shim/generated` 里对应的
生成头也早就是 19 个 —— 缺的从来不是生成，是 bridge 那侧的 codec。
`bridge/windows/src/action_registry.cpp` 544 → **727 行**：`registry()` 9 → 20 行、
`applyGoalFields()` 多 11 个分支、`describe()` 每个类型都有逐字段打印器（在此之前
`ros2 action send_goal` 对表里没有打印器的类型只打一行
`(no pretty printer registered for this action type)`）。

【跑】`python tools\run_ctest.py bridge` **4/4、3.5 s**
（`bridge_action_roundtrip`：**373 checks, 0 failure**，改之前是 147 checks / 2 failure）；
`ros2.bat interface list` **85 → 96** 个类型；`ros2 action send_goal /sim/wait
nav2_msgs/action/Wait time.sec=1` 打出完整 goal / 10 条 feedback / result；
完整验收 `acceptance_check.py` **9/9**。

**这条不能证明什么**：`ros2 action list -t` 仍然只有 5 条 —— 那张表列的是**远端声明了什么**，
仿真的 Python 对端只起 5 个 action；20 条 codec 的意义是「真 nav2 机器人声明的那些现在也能调」，
不是「本机凭空多出 15 个 action」。真 ROS2 对端（50111）这一轮仍然不在线，所以 19 条里只有
5 条跟对端往返过（证据 24）。

### 二、`shm_transport` 挂了 639 s，而树里没有一条 `TIMEOUT`

【跑】`python tools\run_ctest.py bridge` 这一次**没有返回**：前三条 0.02 / 2.08 / 0.99 s 全过，
第四条 `shm_transport` **639 s、0% CPU、一行输出都没有**（手动 `taskkill` 才断）。
单独 `-R shm_transport`：**0.96 s 通过**；紧跟着整棵树重跑：**4/4、3.5 s**。

【判】症状与 `nav2/shm_transport/tests/test_shm.cpp` 里那段注释
（`channels left behind by a writer that is gone`）同类：**写者被强杀之后留下的通道，让下一个
跑到这里的进程等一个永远不会来的唤醒**。这一轮现场里那个被强杀的写者是守护进程（重编 bridge
前后 `ros2_bridge.bat stop` + `taskkill` 反复了几次）。**根因没有定位到函数级**：没有栈、
没有 core，两次运行的差别只有「上一次有没有人被杀掉」。

【改】给它一个上界，而不是继续查：`nav2/shm_transport/CMakeLists.txt`、
`nav2/ros2_shim/CMakeLists.txt`、`bridge/windows/CMakeLists.txt` 三处的
`set_tests_properties` 都加了 `TIMEOUT 300`，`ctest --show-only=json-v1` 逐条查过：
三棵树 **37 条测试全部带上**。这不是「修好了」，是「挂死从无限等待变成五分钟的失败，
并且带上测试最后那几行输出」。`gRPC` 那棵树的 4 条没加（另一条路径，没观察到同类现场）。

### 三、还开着的一条

* **`python tools\run_ctest.py sim` 在本机跑不起来**：它用 `sys.executable` 起 pytest，而本机
  默认的 `python`（不是 venv）里没有 pytest，输出是 `No module named pytest`。CI 里那一步先
  `pip install pytest` 再跑，所以那条路是通的；本机要用
  `E:\venvs\py312\Scripts\python.exe tools\run_ctest.py sim`（**71 passed**）。这属于
  「同一个入口在不同机器上行为不一致」，和第二十二节第一条（CI）是近亲。

## 二十四、`Image` / `CameraInfo` 入表（2026-09-17 夜）

第二十二节第三节排的第一条（`sensor_msgs/Image`）关掉了，过程与实测在 `03-validation.md` 证据 62。
这一节只记三件别处没有的东西：**这块板的成本结构**、**顺路撞出来的两条 CLI 语义**、
**关掉之后长尾变成什么样**。

### 一、相机入表 = 一处表项、三处联动

上游 `nav2/ros2_shim/generated/include/sensor_msgs/msg/image.hpp` 与 `camera_info.hpp` **早就生成好了**
（29 个消息包全在），缺的一直只有 `bridge\windows\src\type_registry.cpp` 里那三处：`describe()`
（`topic echo` 打什么）、`applyFields()`（`topic pub` 认哪些字段）、`registry()`（表项本身）。
956 → **998 行**，话题表 38 → **40**，`interface list` **96 → 98**。

两个技术点值得留下：

* `Image::data` 是 `std::vector<uint8_t>`，一张真图百万字节量级，所以 `describe()` 打的是
  摘要（`[15 values, min 1, max 15]`）而不是内容 —— 用表里已有的 `summarize()`；
* `CameraInfo` 的 `d` / `k` / `r` / `p` 是 `std::array<double, N>`，走 `setSequence` 的
  **定长**分支（逗号分隔、长度不符拒收），和 nav2 那些数组字段同一套拼法，**没有新增语法**。

fixture **35 → 37**（`image` cdr=83 / shim=73、`camera_info` cdr=373 / shim=362），
`bridge_cdr_check` **37 fixtures / 111 byte comparisons / 0 failure**；
`bridge` 树 **4/4**；完整验收 **9/9**。`bridge_cdr_check` 读 `manifest.tsv` 遍历，
所以测试源码一个字没动就吃到新 fixture —— 这是第二十二节说的「体力活」里唯一省下来的一步。

### 二、链接失败 ≠ 代码错：守护进程占着 `ros2_bridge_daemon.exe`

第一次重编的现场是 `ninja: build stopped: subcommand failed`，失败目标还被 `Select-Object`
截掉了，看上去像新加的 `Image` 分支编不过。实际是**守护进程还在跑**，链接器写不进那个 exe；
`ros2_bridge.bat stop` 之后再编，`1/1` 通过，一行代码没改。

**这条是这一轮唯一的「假警报」**，代价是几分钟。根因不在代码里，在流程里：
`build_bridge.py` 不保证守护进程已停，而 ninja 把「文件被占用」和「编译错误」报成同一个形状。
建议（**没做**）：`build_bridge.py` 在 `cmake --build` 前自己 `stop` 一次，或在链接失败时检测
「daemon 还在跑」并明说。它和第二十三节那个 639 s 挂死是近亲 —— 都来自
「重编前后反复起停守护进程」这个动作。

### 三、`ros2 topic echo` 不等发布者

实机往返时按真 ROS2 的习惯「先 echo 后 pub」，`echo` **立刻**退出：
`cannot open '...': OpenFileMapping failed: channel does not exist`。真 ROS2 的 `topic echo`
会等到一条消息为止。这里的语义是**通道必须先存在**，所以顺序只能是「守护进程 / 发布者先，
echo 后」。`topic pub` 的那句 `nobody was holding '...' yet, so a --once publish may be missed`
是同一件事的另一半：**通道的存在性就是这一层的「图」**。

属于 B 节（兼容层的 ROS2 表面）的一条已知差异，不是这一轮引入的；记在这里是因为它会让
第一次用的人以为「桥没通」。

### 四、关掉之后，长尾变成什么样

* **相机一族只补了 `Image` + `CameraInfo`**：`CompressedImage`、`PointCloud` 与其余传感器变体
  仍在表外；
* **41 个话题 codec 里有 3 个没有逐字节 fixture**（第二十二节第三节已展开）：
  `visualization_msgs/msg/MarkerArray` 与 `std_msgs/msg/Empty` 写了理由，
  `nav2_msgs/msg/Costmap` **是唯一没写理由的** —— 它只在 service 那一侧被 `GetCostmap` 的
  打印器覆盖，没有逐字节对照（第四个 `action_msgs/msg/GoalStatusArray` 已在证据 73 里补上）；
* 第二十二节其余条目（CI 没跑过、短体补零、真对端验收）**没有变化**；
* **跨机的相机通路没实测**：仿真的 Python 对端不发相机话题，「Ubuntu 发图 → Windows 收图」
  这一步只有 fixture 级别的证据，没有活链路。

## 二十五、P0-1 的第二个入口：连不上 agent 时日志仍然无界（2026-09-17 夜）

第二十二节没有列这一条（它属于已经关掉的 P0-1），但它是一个**复核出来的新缺口**，
过程与实测在 `03-validation.md` 证据 63。

`Bridge::run()` 有两个循环：扫描重试循环和控制循环。日志的十秒 tick 原来只挂在后者上，
而后者要连上 agent 才进得去 —— 于是 **agent 不在时日志无界增长**（实测：cap 200、90 s、
783 字节、零轮转）。修法是把这个 tick 提到扫描循环之前，并在重试循环的 100 ms 睡眠里
跑同一段检查；`bridge\windows\src\bridge_daemon.cpp` 1927 → 1941 行。

**这条现在是关的**：连不上（42 s，两个 `.1`、公告 2 条、0 失败）和连得上（35 s，`.1` 4278 B、
公告 1 条、0 失败）两条路径都实测过，`bridge` 树 4/4、完整验收 9/9。

还开着的小尾巴（都写在证据 63 里，都不是缺陷）：

* 非分离模式（`ros2_bridge.bat run`）没有日志文件可管 —— stdout 是调用者的控制台，
  这不是「没修」，是**不该动**；
* 上限是「活的那个文件可以超出 cap 一个十秒窗口」，所以稳态是 `[cap, 2 × cap)` 而不是
  硬上限。`ROS2_BRIDGE_LOG_MAX=0` 可以整个关掉这个检查。

## 二十六、CLI 取值：`k=v` 与真 YAML 的差距（2026-09-17 夜）

B 节那张表的「CLI 面」一行原来只写「有哪些子命令」，没写**取值语法**。这一节补上：过程与实现在
`03-validation.md` 证据 64。

**改前**：`topic pub` / `service call` / `action send_goal` 只认 `field.path=value`，真 ROS2 的那份
YAML 一律被打回去：

```
ros2 topic pub /shim/t1 geometry_msgs/msg/Twist {linear: {x: 0.5}} --once
  → [ros2] unexpected argument '{linear: {x: 0.5}}'
ros2 topic pub /shim/t2 geometry_msgs/msg/Twist {linear.x: 0.5} --once
  → [ros2] unexpected argument '{linear.x: 0.5}'
ros2 topic pub /shim/t3 sensor_msgs/msg/LaserScan ranges=[1.0, 2.0, 3.0] --once
  → cannot build sensor_msgs/msg/LaserScan: invalid list for ranges: '[1.0, 2.0, 3.0]'
ros2 topic pub /shim/t4 std_msgs/msg/Bool data=True --once
  → cannot build std_msgs/msg/Bool: invalid value for data: 'True'
```

其中一条不是报错，是**静默错值**：`data="hi"` 退出码 0，发出去的是**带引号的两个字符**
（`topic echo` 打 `data: ""hi""`）。cmd 会把 `"` 吃掉、PowerShell 不会，所以同一个命令在两种 shell
里发的东西不一样 —— 这类问题比报错难查得多。

**改后**：新增 `bridge/windows/include/ros2_bridge/yaml_assign.hpp`，把一段 YAML 摊平成同一组
`FieldAssign`，三条子命令共用；bool 字段的 `true/false/yes/no/on/off`（不分大小写）在
`field_parse.hpp` 里认下。两种写法可以混用，`bridge` 树多了一条 `bridge_cli_values`（194 条断言，
`ctest` 里 5/5）。顺带把三个子命令的短选项补齐（`-1` / `-r` / `-t` / `-p` / `-f`），并且
**不认识的选项不再被当成话题名**。

**块式缩进是同一天晚些时候补的**：`linear:` 换行再 `  x: 0.5` 这种（从 `--params-file` 里拄一段
过来最常见的形状）原来会被当成「字段名 `linear` 后面没值」；而多行的 `a: 1` / `b: 2` 更糟 —— 裸标量
原来不认行尾，第二行会被整段吞进第一个值，rc 还是 0（详细过程在证据 64 的第四节）。
现在块式的键靠换行分隔、子块靠缩进嵌套，裸标量到行尾结束。

还开着的（都不是这一轮的目标，但要知道）：

* ~~**映射数组进不来**~~ —— **补上了**（证据 68）：`{points: [{x: 1}]}` 与
  `poses.goals[0].pose.position.x=1.5` 两种写法都认，按序列首元素判定它是「标量列表」还是
  「每条元素一个消息」，混着写当场报错。仍然开着的是「一个序列只有一种元素形状」——序列套序列不做；
* **`--stdin` 没有**：真 CLI 可以从标准输入读那段 YAML；
* **`-w/--wait-matching-subscriptions` 没有**：`--once` 遇到「通道还没人持有」只打印警告（B 节那条），
  `-w` 才是「等到有人订阅再发」；
* **`service call -r`、`topic echo --field` / `--qos-*` 没有**：清单在 B 节的「CLI 面」那一行。
* **块式的序列不认**：`ranges:` 换行再 `  - 1.0` 那种（YAML 的 block sequence）解析器不吃，
  序列要写成 `ranges: [1.0, 2.0]` / `ranges=[1.0, 2.0]`。锚点 / 别名 / `|` `>` 折叠块 / 多文档
  同样不做 —— 真 CLI 用的是 PyYAML，这些它都收；

## 二十七、进程台账：`racecar_ctl.py stop` 一直停不掉 SLAM 节点（2026-09-19）

`racecar_ctl.py` 的 `status` / `stop` 靠“这个映像名在不在跑”来答，而它的判据
（`tasklist /fi "status eq running"`）在本机命中 87 个无关进程、**自己人一个都不在里面**
—— 连正在跑的 `ros2_bridge_daemon.exe` 都是 `Unknown`。`stop` 拿同一个函数当守卫，于是 `slam` 建图
跑完之后 `stop` 会**整段跳过** SLAM 那一路。过程与实测在 `03-validation.md` 证据 69。

改法：`_bridge_platform.py` 新增 `live_pids()`（两平台同名），Windows 侧用
`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` + `QueryFullProcessImageNameW` 问进程自己
—— `taskkill /F` 留下的僵尸条目回 `ERROR_GEN_FAILURE (31)`、活进程答自己的映像路径；
`kill_nodes()` 的 Windows 分支从 `taskkill /F /IM` 改成先解析成 pid 再 `/PID`。
`racecar_ctl.py` 的 `process_running()` 改成问同一份平台层。

还开着的：

* **僵尸条目删不掉**：`taskkill` 对它们回 `Access is denied`，只能等系统回收或重启。
  这一条改的只是“不再把它们当节点”，不是“把它们清掉”；
* **`bridge/windows/*.bat` 的 `:kill_nodes` 仍是 `tasklist | find` + `taskkill /im`**：它杀得掉活节点
  （那不是守卫，只决定要不要打 note 和等两秒），代价是机器上一个节点都没跑时
  `nav2_stack.bat stop` 仍会打 `stopping the previous stack`。纯 cmd 拿不到上面那个判据，
  要一致就得让 `.bat` 去调 `live_pids()`；
* **Linux 那一半没端到端实测**：`live_pids()` 在 Linux 上就是原来的 `_linux_pids()`
  （`/proc/<pid>/cmdline`），本轮只在两端编过、跑过 ctest。

## 二十八、racecar 应用层接上兼容层之后，还开着什么（2026-09-19）

证据 70 把 `code/Desktop/racecar/src/racecar/scripts/` 那 19 个脚本接进来了：13 个
`NavigateThroughPoses` 的 goal 字段从 Humble 的 `poses` 改成 Jazzy 的 `poses.goals`，7 处
`/home/davinci-mini/...` 换成相对脚本目录，另加 `racecar_ctl.py nav --map live` 和一条仿真房间
用的 `sim_room12.csv`。下面按「会挡住谁」排**那次改动之后仍然开着的**。

### 1. ~~`test.csv` 不在仓库里，4 个脚本跑不起来~~ **2026-09-19 修掉了**

`end_3_12.py`、`会跳过几个点.py`、`跑一圈以内.py`、`重复载入完成圈数.py` 读的是
`<脚本目录>/test.csv`。车上原本有（老路径 `/home/davinci-mini/racecar/src/racecar/scripts/test.csv`），
仓库里只带了 `ai_test.csv` / `ai_test1.csv` / `ai_test2.csv`。路径这一半已经修好，**文件那一半没有**，
所以这 4 个脚本一跑就是 `FileNotFoundError`。
**成本**：从车上拷回来，或者把它们指向已经带进来的那三条航线之一。

**这一轮没有二选一，两条都留下来了**：那 4 个脚本（连同 `go copy.py`、`一条路径+csv.py`、
`终点不停.py`，一共 7 个）现在都走 `go.py` 早就有的那套 `waypoint_file()` ——
`run <脚本> <名字>.csv` 给的名字优先；不给就按 `test.csv`、`ai_test.csv` 的顺序找脚本目录里的第一个
存在的文件。于是**车上把 `test.csv` 放回原位就还是原来的行为**（它在第一顺位），仓库里没有这个文件
也不会再抛异常。**没有关掉的是「车上那份航线不在仓库里」**，只是不再挡住运行。

### 2. ~~12 个改了字段的脚本一个都没实跑~~ **2026-09-19 补上了：17 个里 16 个跑过**

证据 70 只跑了 `go.py`。其余 12 个改的是同一行、`compile()` 全过，但它们各自还有别的分支：
`Trigger` 服务调用（`/sim/reset`）、`self.poses` 重发、按 4 个一组分组推进、圈数计数。
「编译过」和「跑得过」在这套兼容层上是两件事 —— 这一轮的第一个 bug 就是编译完全没问题、
一序列化就炸。
**成本**：每个脚本对着 `sim_room12.csv` 各跑一次，看 result 里的 `waypoint_statuses`。

证据 76 跑了其中 2 个：`一条路径+csv.py`（76.1 s，9/9 `waypoint_status=1`，`error_code=0`）与
`终点不停.py`（73.8 s，同上，顺路把 `stop` 服务那处死等改成有界等待）。仍然只有 `compile()` 的是
这 10 个：`ai_第一个点后回终点.py`、`detct.py`、`end_3_12.py`、`goal_l.py`、`go一条路径.py`、
`人工智能赛道初始版本代码.py`、`会跳过几个点.py`、`第二个点nav2打断，然后转圈圈.py`、
`跑一圈以内.py`、`重复载入完成圈数.py`。`self.poses` 重发、按 4 个一组分组推进、圈数计数这三条分支
就是它们各自多出来的东西 —— 跑过的那 2 个都不含这三条。

证据 77 把剩下的 10 个也跑了（`.scratch/run_mission_batch.py`：每个脚本走完整的一套
`stop` → `/sim/reset` → `nav --map live` → `run`），**跑起来当场崩了 3 处，全修了**
（`.scratch/script_fixes.txt`）：4 个脚本的 `cmd_vel_callback` 少了 `distance_remaining is not None`
守卫（`/cmd_vel` 可以比第一条 feedback 先到，`TypeError` 之后 0.3 s 就退出）、5 个脚本的航线名写死
（补上 `go.py` 那套 `waypoint_file()`，默认值还是各自原来那一条）、1 个脚本读的是 Humble 才有的
`result.navigation_time`。17 个脚本里现在 **16 个在兼容层上真跑过**，每个脚本一份日志、结论表在
`.scratch/batch_results.txt`，只剩 `get.py`（它要键盘，是给人按的工具）。

另外 3 个脚本本来就不在这 13 个里，这一轮也没跑：`go copy.py` 走的是 `/goal_pose` 而不是
action（`nav2_bt_navigator` 的 `NavigateToPoseNavigator` 确实订阅它并转成 goal ——
`navigate_to_pose.cpp` 第 52 行 —— 但**兼容层上这条入口一次都没走过**），`go_2c.py` 与
`go copy_多点需要切换.py` 走 `BasicNavigator`（它的 action 结果面在证据 74 里单独跑过，脚本本身没有）。

### 3. ~~`nav2_simple_commander` 的结果面还没走完~~ **2026-09-19 走完了**

`go_2c.py` / `go copy_多点需要切换.py` 走 `BasicNavigator`。2026-09-19 把它的 action 面单独拉出来
跑过（`.scratch/dbg6_counters.py`）：`BasicNavigator()` 构造（一次建起 17 个 `ActionClient`）、
`nav_pose_client.send_goal_async(goal)`、`rclpy.spin_until_future_complete` 等到 ack、goal handle 的
`accepted`，都跑得通。对照臂是同一个进程里另建一个只有单个 client 的普通 `Node`，两者结论一致。

**这一轮因此推翻了一条被写下来的错误结论。** `[ERROR] [basic_navigator]: NavigateToPose goal to
1.178 -1.089 was rejected!` 原先被归因成「Python 客户端读 ack 的布局不对」，实测不是：

* `compute_path_to_pose`（planner_server，空闲）→ `accepted=True`，8 s 内拿到 ack；
* `navigate_to_pose`（bt_navigator，正在跑上一个 goal）→ `accepted=False`，而且 bt_navigator 日志里
  同一时刻确实写着 `Requested navigation from navigate_to_pose while another navigator is processing,
  rejecting request.`（这是 upstream 的行为，不是兼容层的）。
* 两端的环计数器对得上：`_goal_ack` 的 `published_count` 在 ack 返回时 +1，客户端的 `read_count`
  同步 +1；`_goal` 侧同样。

也就是说客户端是对的，拒绝是真的。真正的缺陷在第十一节。

结果面现在也跑完了（证据 74）：`setInitialPose` / `waitUntilNav2Active` / `goToPose`（`SUCCEEDED`，
6.8 s）/ `getFeedback`（2.27 m → 1.04 m）/ `getResult` / `cancelTask`（`CANCELED`，0.3 s 后）/
`clearAllCostmaps` / `lifecycleShutdown`（10.6 s，管理器记 `Managed nodes have been shut down`）
八条逐条过。**`go_2c.py` / `go copy_多点需要切换.py` 本身仍然没实跑**（那归上一条）。

留下两条**不是兼容层的**新记录：

* **`BasicNavigator.initial_pose` 是默认构造的 `PoseStamped`，四元数 (0,0,0,0)**，而
  `nav2::validateMsg` 要求 `|‖q‖-1| < 1e-4`（`nav2_ros_common/validate_messages.hpp`）。
  所以 `waitUntilNav2Active()` 的默认 `localizer='amcl'` 那条路会**永远**循环下去：AMCL 每一轮
  都答 `Received initialpose message is malformed. Rejecting.`，`initial_pose_received` 永远不翻。
  先 `setInitialPose(pose)` 就好了（或者 `localizer='robot_localization'` 跳过这一半）。真 ROS2 上
  同样如此 —— 第二节的 A/B 就是为了证明这一点；
* **`spin_once(timeout_sec=N)` 只睡 1 ms**：`ros2py/executor.py` 的 `IDLE_SLEEP = 0.001`，而
  `_poll()` 把它当成 `min(IDLE_SLEEP, N)`。实测 `timeout_sec=1.0` 每调用 **1.6 ms**（差 625 倍）、
  `0.1` 差 133 倍。共享内存环没法 `wait`，这个上限本身是有意的，但后果是上游那种「等一条消息」
  的紧循环变成 ~600 Hz：上面那个坏掉的等待在 27 s 里写了 **2,058,378 字节**日志。写脚本时别把
  `spin_once(timeout_sec=...)` 当成睡眠用。

### 4. ~~`_action/status` 这条隐藏话题没有~~ **2026-09-19 补上了**

原文：`nav2_waypoint_cycle/waypoint_cycle.py` 订阅 `navigate_to_pose/_action/status`
（`action_msgs/msg/GoalStatusArray`）来决定什么时候推进下一个航点，而兼容层的 action 只实现了六条
通道（`_goal` / `_goal_ack` / `_cancel` / `_cancel_ack` / `_feedback` / `_result`），这条既没有发布者
也不会被反射出来，`ros2 topic list` 里就没有这个名字。

现在 server 侧会建 `name + "/_action/status"` 的发布者（`QoS(1).transient_local()`，槽走
`parameter_slot_size()`，两份拼写分别在 `shm_config.hpp` 的 `is_action_status_name()` 和
`ros2py/channel.py` 的 `slot_size_for_topic()` 里）。goal 从 ACCEPTED → EXECUTING → 终态各发一条，
保留 10 s / 64 条。`bridge/windows/src/type_registry.cpp` 认了
`action_msgs/msg/GoalStatusArray`（接口表 98 → **99**，话题 40 → **41**），`interop_matrix.py` 的
第六条腿探它，`tests/action_status.cpp` 是它的 ctest。逐条证据见 03-validation.md 的「证据 71」；
它现在也有逐字节 fixture（`bridge\tests\fixtures\goal_status_array.cdr` / `.shim`，证据 73）。

### 5. Python 侧仍然没有 action **server**

`ros2py/action.py` 只有 client。要在兼容层上用 Python 写一个 action 服务端，得先有「谁在订阅
`_goal` 通道」这一侧（SelfClient 那套）。C++ 侧有（nav2 那 20 个 action 都是服务端），Python 侧没有。
**成本**：`ActionServer` + goal handle + 取消语义，一块新东西。

### 6. QoS 只声明不生效

`rclpy.qos` / `ros2py.qos` 的 `QoSProfile` / `ReliabilityPolicy` / `DurabilityPolicy` / `HistoryPolicy`
都能构造，`create_publisher(..., qos_profile)` 也收得下，但底层共享内存通道没有可靠性、持久性、
深度这三个语义 —— `transient_local` 不会让后到的订阅者拿到最后一条，`reliable` 也不会重传。
`nav.yaml` 里那些 `qos_overrides` 同理，是读进来放着的。**别把它当成能用了。**

### 7. `--map live` 只有 `shim` 后端有

`racecar_ctl.py` 的 `ros2` 分支直接走 `resolve_map(wanted)`，`--map live` 会被当成一个找不到的图名，
报的是「`map/` 里都有什么」而不是「这个后端不支持」。真车上不需要它（`/map` 本来就是 `map_server`
在读一个文件），所以合理的收尾是**明说**：要么在 `ros2` 分支里拒绝这两个保留名，要么在 `--help` 里
写清它是 shim-only。

### 8. 「真值图配不上正在跑的仿真世界」这件事还是手工的

`run_sim.py --seed` 默认 `None`，所以每次重启仿真器就换一间房，`map/` 里那些 yaml 都只是快照
（实测差 15.5% 的像素，证据 70 第四节）。`--map live` 是补救，但「这张图是哪次采摘的」还是靠人记。
**能改进的一条**：让仿真器把自己的种子报出来（`/sim/status` 那行的一句话里再加一个字段就行），
这样至少能判断盘上的图是不是当前这一间的。

### 9. `--map live` 要重启两次守护进程

`capture_live_map()` 先 `restart_daemon([])`（镜像一切，`/map` 才拿得到），dump 完再
`restart_daemon(MIRROR_WITHOUT_MAP)`（`/map` 每边只能有一个写者，接下来那个写者是 `map_server`）。
两次重启之间有几百毫秒没有镜像，`nav` 那一条命令因此比不带 `--map` 慢几秒。
**成本**：低。真要做可以给守护进程加一个「暂停镜像某条话题」的控制面，但没有证据说这几秒值钱。

### 10. 一次性 `topic pub --once` 喂不到晚挂上来的订阅者

`ros2 topic pub --once` 是一次性进程：发布者一退出，它建的那条环就没了，晚起来的订阅者从
`seek_to_latest` 之后只能看到空的。所以 `ros2 topic pub --once /initialpose ...` 之后再起 AMCL 是拿
不到的；`/clicked_point` / `/goal_pose` 这类**发布者活着**的交互式用法不受影响。要喂一条一次性的
消息给还没起来的订阅者，得用 `--rate N --duration S` 让发布者在那里等。（证据 71 第九节实测。）
**成本**：低。真要做可以给 `topic pub` 加一条「等订阅者出现再发」的开关。

### 11. ~~一个规划不了的 goal 会把 `bt_navigator` 卡死~~：入口那一环已修，剩下的两环要先「仿真器重启过」才发得出来

这是 2026-09-19 找到的最重的一条，也是 `go_2c.py` 三个点全部 `was rejected!` 的真正原因
（`%TEMP%\nav2_stack\*.log`）。链条三段：

1. **规划失败抛的是 `std::bad_alloc`（2026-09-19 定位并修掉）。** 对一个到不了的 goal
   （实测目标 `(1.00, 1.50)`），`SmacPlannerHybrid` 当时抛 `std::bad_alloc`，planner_server
   日志里 376 次、188 次 `Aborting handle`；那确实是「某处算出了一个荒谬的长度交给 `new`」
   —— 规划器自己的角度箱越界，根因与修法见 A 节。现在同一个 goal 的答复是
   `PlannerTimedOut`（error_code 207、~1.7 s），动作正常结束；而且**坏目标之后的下一个 goal
   立刻受理并跑完**（实测先发一个墙里的 goal 拿到 `error_code 206`，紧接一个好 goal 走 4 m、
   19 s `SUCCEEDED`，见证据 72 第八节）。所以「一个坏 goal 就毁掉整栈」这一条不再成立。
2. **BT 走恢复分支，轮转到 `Wait`。** `nav2_behaviors::Wait::onRun` 存的是
   `node_->now() + command->time`，**绝对仿真时刻**（BT 里是 `<Wait wait_duration="5.0"/>`）。
3. **仿真器一重启，`/clock` 的纪元从头开始。** 实测：仿真进程已经跑了 4 h 54 min、纪元 13000 s，
   而 `/clock` 只报 17381 s —— 中间少掉的那 13000 s 就是一次重启。第 2 步存下的 `wait_end_` 于是
   落在**未来约 13000 s**，`onCycleUpdate` 每次都返回 `RUNNING`，这个 5 秒的 `Wait` 要等 3.6 小时。
   BT 的 `<RecoveryNode number_of_retries="6">` 再也数不到 6，goal 永远不结束，
   `bt_navigator` 永远「processing」，于是后面每个 goal 都被拒。

第 2、3 步本身没变：要复现这条链，得让一个 goal 先规划失败、**再把仿真器重启一次**让 `/clock`
回跳。所以「仿真器重启之后必须重启整栈」这条建议不变，它管的是这两步。

顺带暴露了一段 upstream 代码的脾气，值得记下来：`SimpleActionServer::deactivate()` 里那个
`while (execution_future_.wait_for(milliseconds(100)) != std::future_status::ready)` **没有出口** ——
超时分支只打日志不 `break`（`nav2_ros_common/include/nav2_ros_common/simple_action_server.hpp`
337-344 行）。所以行为服务一旦在动作还在跑的时候被 deactivate，它就在那里 100 ms 一转地刷
`Waiting for async process to finish.` + `Action callback is still running and missed deadline to stop`
（实测 11113 次），进程再也退不出来 —— 这也是 `racecar_ctl.py stop` 停掉整栈之后
`behavior_server.exe` 还活着的原因。

**能做的两条**：

* **便宜、立刻见效**：仿真器重启之后必须重启整栈（`racecar_ctl.py stop` 再 `nav`）。栈里每一个
  `use_sim_time` 的节点都握着旧纪元的绝对时刻（`Wait`、AMCL 的超时、控制器的 `simulate_ahead_time`
  都是）。这条建议已经写进 `code/Desktop/racecar/README.md`。
* **根治**：让兼容层的时钟订阅在发现 `/clock` **回跳**时报一行错（`ros2py/clock.py` 记一个
  `last_seen`，回跳就 `error`），至少让下一次的现象是「时钟回跳」而不是「nav2 忽然不动了」。

### 12. ~~航点的朝向被写丢了：12 个脚本把 CSV 的第 3 列塞进了 `position.z`~~ **2026-09-19 量过了、也改掉了**

**2026-09-19 的补充**：先在兼容层上把这一列的效果量出来（`.scratch/probe_orientation.py` →
`.scratch/probe_orientation.txt`），`sim_room12.csv` 第 5 行（x=1.20 y=-3.00，朝向要 +115.02°）：

```text
old    position.z = col3, quaternion (0,0,0,col4)    ended yaw=-55.65 deg  (差 170.67°, 17.5 s)
fixed  quaternion (0,0,col3,col4)                    ended yaw=+61.14 deg  (差  53.87°,  3.4 s)
```

剩下那 53.87° 落在 `nav.yaml:138-139` 那两个 1.0 rad（57.3°）容差里 —— 旧写法不是「差一点」，
是**从头就没往那个朝向走**（17.5 s 里有 14 s 花在贴一个没被赋值的朝向）。12 个文件各改一行
（`position.z` → `orientation.z`，每个都 `assert` 只出现一次、无 BOM、LF），清单与「没改的 5 个
为什么不用改」见证据 80 第三节。**改完没有 12 个全部重跑对朝向**，这一条的验证强度是
「一个航点的 A/B + 12 个文件同一行的确认」。

13 个 `NavigateThroughPoses` / `NavigateToPose` 脚本里，有 12 个用的是同一段：

```python
pose.pose.position.x = point["x"]
pose.pose.position.y = point["y"]
pose.pose.position.z = point["z"]      # 这一列是四元数的 z，不是高度
pose.pose.orientation.w = point["w"]
```

`sim_room12.csv` / `ai_test*.csv` 的第 3、4 列是一对 `(sin(θ/2), cos(θ/2))`（`sim_room12.csv` 第一行
`-0.1601822430069672, 0.9870874576374967`，平方和正好 1）。所以**朝向的 z 从来没被赋值**，一直是 0，
而那个正弦值进了地面机器人上恒为 0 的 `position.z`。发出去的 goal 四元数是 `(0, 0, 0, cos(θ/2))`，
模长 `|cos(θ/2)|`，只有 θ=0 时才是 1。

**原意是清楚的**，不用猜：同一个目录里的 `go copy.py` 写的就是对的那一行 ——
`self.goalMsg.pose.orientation.z = self.goalListZ[self.goalId]`（`go copy.py` 唯一不发 action、而是发
`/goal_pose` 的脚本）。另外 3 个（`go一条路径.py`、`go_2c.py`、`go copy_多点需要切换.py`）航点写在
代码里，直接 `orientation.w = 1.0`，不涉及 CSV 的这两列。

这套兼容层**收下了**这个半四元数：证据 76 两趟的 feedback / result 里每个航点原样回显
`position=(x, y, -0.1601822430069672)`、`orientation=(0, 0, 0, 0.9870874576374967)`，9/9 通过、
`error_code=0`。对照是证据 74 里 `BasicNavigator` 那个全零四元数被 `nav2::validateMsg` 拒了 ——
校验是真的，只是没挡这一条。

**成本**：一行（`orientation.z = point["z"]`，把 `position.z` 那行删掉）。
**这一轮没改**：这是车上原有的写法，改了之后**每个航点的收尾朝向都会变**，而这一轮手上没有车上的
对照数据 —— 车上是不是也这么发、是不是也收，未知。要动它得先在车上量一次。
`code/Desktop/racecar/README.md` 的坑列表里也记了这条。

### 13. `nav2_waypoint_cycle` 在兼容层上没有对应物

车上的 `Run_nav.launch.py` 除了 nav2 bringup，还会起第三个节点：
`Node(package='nav2_waypoint_cycle', executable='nav2_waypoint_cycle', ...)`（源码在 `src/nav2_waypoint_cycle/`，
跟 `racecar` 一起编的 Python 包）。它做的事是订阅 `/clicked_point` 攒航点、订阅
`navigate_to_pose/_action/status` 判终态，然后自己往 `/goal_pose` 发 `PoseStamped` —— 也就是
「点几个点，车自己一圈一圈跑」。

兼容层这边，`racecar_ctl.py nav` 走的是 `bridge/scripts/run_nav2_stack.py`（planner / controller /
bt_navigator 那一串直接起），**不读 launch**，所以这个节点不会被拉起来。它本身只 import
`rclpy` / `geometry_msgs` / `visualization_msgs` / `action_msgs`，四个都在兼容层已支持的包里
（`<action>/_action/status` 那条通道本来就是为它做的，见 `nav2/ros2_shim/CMakeLists.txt`），
所以缺的只是「谁来起它」。

**成本**：一条 `shim_run.py` 的启动项，加一次 `/clicked_point` → `/goal_pose` 的闭环实测。

### 14. `maps/ceshi1.pgm` 是 15 MB，而它现在会随包 install

`CMakeLists.txt` 这一轮补上了 `map/` 和 `rviz/`（`Run_nav.launch.py` 的默认地图就在 `map/` 里）。
`map/` 里两张图一共 ~500 KB，但 `maps/ceshi1.pgm` 是 **16 000 054 字节（15.3 MB）**，
而 `maps/` 也在 install 列表里 —— 一个包里同时装进去两份地图目录。

这不挡任何事（`colcon build` 只是拷文件），但 `maps/ceshi1.yaml` 和 `map/ai_map.yaml` 都是能用的图，
而且 `ceshi1.yaml` 的 `image:` 刚被从绝对路径修成相对路径。要减包就从 `install(DIRECTORY)` 里
拿掉 `maps`（它是车上留下的旧图）；要留着当第二张测试图也行。这一轮留着了。

### 15. ~~圈数分支（4 个脚本）现在只知道为什么没跑，还没改~~ **2026-09-19 三个换成 `FollowWaypoints` 并各跑两圈**

**2026-09-19 的补充**：车上的 `FollowWaypoints.action` 先看过了 —— `uint32 number_of_loops` /
`uint32 goal_index` / `geometry_msgs/PoseStamped[] poses`，与兼容层的绑定逐字一致，所以三个脚本
（`重复载入完成圈数.py`、`会跳过几个点.py`、`ai_第一个点后回终点.py`）可以两边同一份代码。
第四个 `跑一圈以内.py` 没动：它不是圈数脚本。整体重写之后的形状（`number_of_loops = laps - 1`、
`--laps N`、feedback 只读 `current_waypoint`、收到 result 之后发零速并 `rclpy.shutdown()`）与两圈实测
（各 `exit=0`、159~163 s、18 条 `Waypoint i/9` 报告、绕回第 1 点一次、`missed_waypoints=0`）
见证据 81 第三、四节。**车上没跑过**：`/follow_waypoints` 要 nav2 起着才有服务端。

证据 77 实测出来：重发同一条航线不会真的跑第二圈（第二趟 0.1 s 回 `error_code=0`、9/9 通过，
车一步没动）。证据 78 把成因查清了 —— **不是兼容层、也不是 `RemovePassedGoals`**：
`controller_server` 的 goal checker 只看**路径的终点**（`config/nav.yaml` 的 `xy_goal_tolerance`
是 1.0 m），而车正停在上趟的终点上，于是第二趟第一拍就 `Reached the goal!`，中间八个航点一个
都不看。同一份机制也解释「不 `/sim/reset` 就 0.09 s 假成功」和「首尾相接的 CSV 走不动」——
三条现象一个成因。

能修，而且已经验过怎么修：**圈数走 `/follow_waypoints`**（`nav2_msgs/action/FollowWaypoints`，
它一个航点一个航点地判到达，`number_of_loops = N` 就是走 N+1 遍）。同一条九点航线重发，
第二趟真跑 110.6 s、九个点全过、无 `missed_waypoints`；`number_of_loops=1` 真的走两个来回
（208.92 s，4167 条 feedback，`current_waypoint` 从 8 回到 0 又走一遍）。都是经兼容层跑的。

**没做的是改那几个脚本本身**：`会跳过几个点.py`、`重复载入完成圈数.py`、`跑一圈以内.py`
（其实是四个，`ai_第一个点后回终点.py` 是同一个闭环写法）是车上正在用的代码，把「重复发目标」
换成 `FollowWaypoints` 会动到它们每一圈的行为；而且 `FollowWaypoints.number_of_loops` 是 Jazzy
这一代才有的字段，车上的 action 定义这一轮没看过（Humble 那代只有 `poses`）。
**成本**：先看车上的 `FollowWaypoints.action`，再把这四个脚本换成 `FollowWaypoints` 客户端，
每个跑到第二圈为止各跑一遍。

### 16. `nav --map live` 起栈时 `map_server` 激活偶尔失败，栈直接半截（2026-09-19 新撞到）

证据 78 的第一次起栈就是这样：

```text
[nav2_stack] starting map_server  (...\racecar\map\truth_live.yaml)
[nav2_stack] could not drive /map_server/change_state transition 3
[nav2_stack] see C:\Users\ADMINI~1\AppData\Local\Temp\nav2_stack\map_server.log
```

那一次 `%TEMP%\nav2_stack` 里只剩一个 `map_server.log` —— `bt_navigator.exe` 那些一个都没起来：
`run_nav2_stack.py` 在激活失败那一步就返回了，**栈是半截的，退出码不等于 0**，而外面的调用
（脚本里是 `cmd`/`;` 串起来的）照样会去 `run`。探针那一趟就是四个 goal 全部「no action server」，
白等 80 s。`map_server.log` 里它自己只走到 `Read map ...: 240 X 240 map @ 0.05 m/cell`，没有别的
报错 —— 看起来是 `change_state` 那一问没等到答复，不是加载失败。**原样重跑一条 `nav --map live`
就好了**（`[nav2_stack] map_server is active`），那一趟和之后两趟都正常。

**成本**：知道是哪一问没答（手动来一遍 `ros2.bat service call /map_server/change_state`，或者把
`run_nav2_stack.py` 那一步的重试与退出码一起写出来）。现在只是「遇到就重跑一次」。这条也是
「起栈脚本该自己重试」的第二个理由 —— 第 9 条是 `--map live` 要重启两次守护进程。

### 17. ~~仓库里那份 `openslam_gmapping` 少了整个 `log/` 模块，整树 `colcon build` 过不去~~ **2026-09-19 补上了，整树 6/6 全绿**

**2026-09-19 的补充**：从上游 `ros-perception/openslam_gmapping` 的 `melodic-devel` 把 `log/`
搬了回来（4 个头 + 4 个 cpp + 一个 `CMakeLists.txt`，只经 GitHub API 抓），并在根 `CMakeLists.txt`
里 `add_subdirectory(log)`。**过程中又多出两个坑**：本拷贝的 `sensorreading.h` 析构写成了非虚
（上游是 `virtual`），而 `log/sensorlog.cpp:129` 要 `dynamic_cast`，clang/gcc 都报
`source type is not polymorphic`，已按上游改成虚析构；`slam_gmapping.h:38` 的
`tf2_geometry_msgs.h` 在 Jazzy 里没有了，改成 `.hpp`。`lslidar_driver` 另需四个构建依赖：
`libpcl-dev libpcap-dev ros-jazzy-{diagnostic-updater,pcl-conversions,pluginlib}`。
干净重建 6 个包 41.3 s、`CONCOLEXIT=0`，`install/openslam_gmapping/lib/` 下 7 个 `.a` ——
日志 `.scratch/ub_whole_build4.log`，全貌见证据 81 第五节。

证据 79 第三节：Ubuntu 上第一次整树 `colcon build`，停在这个包上 ——

```text
scanmatcher/scanmatcherprocessor.h:4:10: fatal error: gmapping/log/sensorlog.h: 没有那个文件或目录
    4 | #include <gmapping/log/sensorlog.h>
```

不是平台问题，是仓库里这份拷贝缺文件：包里有 3 个文件 `#include` 指着它
（`include/gmapping/gridfastslam/gridslamprocessor.h`、`scanmatcher/scanmatcherprocessor.h` 要
`gmapping/log/sensorlog.h`；`carmenwrapper/carmenwrapper.h` 与 `scanmatch_test.cpp` 要
`log/sensorlog.h`），而 `include/gmapping/` 下只有 `grid gridfastslam particlefilter scanmatcher
sensor utils`，包根也没有 `log/`。上游 `ros-perception/openslam_gmapping` 两样都在
（`include/gmapping/log/sensorlog.h`、`log/sensorlog.cpp`，这一轮用 GitHub API 查的树列表）。
Windows 上应该同样编不过 —— 那台机器上从来没人编过这个包，所以一直没人撞到。

**为什么这一轮不动它**：`openslam_gmapping` 是小车旧的 gmapping，这条线用的是移植的 slam_toolbox
（`nav2/slam_toolbox_port`），兼容层一次都不碰它；而它是 vendor 进来的第三方源码，补文件得先定补
哪一版（这份拷贝是 ROS2 版的布局：头文件就在源码目录旁边、每层一个 `CMakeLists.txt`，跟上游
`melodic-devel` 那份 ROS1 的布局对不上）。**成本**：从车上那份完整拷贝里把 `log/` 拷回来
（只有车上有），或者同步 ROS2 版 gmapping 上游，然后两平台各编一次。

顺带一个陷阱：**任何「排除名为 `log` 的目录」的打包步骤都会正好切掉它**（colcon 工作区自己的日志
目录就叫 `log/`，谁都习惯把它排除掉）。`.scratch/push_racecar.py` 与
`tools/check_cross_platform.py` 都中过这一枪（两处都把 `build/install/log` 当"任何一层见到就跳"），
本轮都改成只跳**顶层**那三个 —— 不然补回 `log/` 之后，下一次推送又会把它吃掉。

### 18. ~~「两边都编」这条门禁现在覆盖应用层了，但本地那一半只是个入口检查~~ **2026-09-19 远端换整树、本地换脚本加载检查**

**2026-09-19 的补充**：远端那一半换成**整树 + 先 `rm -rf build install log`**（增量构建会在
「从零配置不过」的树上变绿，那正是一个依赖没声明的包的样子），后面接四条断言（`lib/racecar/*.py`
数到 17、`go.py` 可执行、`sim_room12.csv` 与 `share/racecar/config/nav.yaml` 在 install 树里）；
本地那一半换成 `code/Desktop/racecar/tools/check_mission_scripts.py`（经 `shim_run.py --path
src/racecar/scripts` 把自己再拉起来，17 个脚本逐个当模块加载、`waypoint_file()` 三种问法、
CSV 清单、第 12 条那行的回归守卫）。实测 54.3 s 两半全绿、负向测试五类全报 ——
证据 82 第五节。

原来的缺口是：`tools/check_cross_platform.py` 推的是 `nav2/ros2_shim`、`nav2/shm_transport`、
`nav2/slam_toolbox_port`、`nav2/third_party`、`nav2/nav2_extracted`、`bridge/*`、
`gRPC/ros2_gRPC_sdk`，六步全在兼容层这一侧；`code/Desktop/racecar` **既不在推送清单里，
也没有一步会在 Ubuntu 上 `colcon build` 它** —— 所以「同一份代码两平台都能编」这句话，对兼容层
成立、对应用层从来没被机器检查过（证据 79 是第一次，还是手工补的）。

**2026-09-19 补上了第七步 `racecar-build`**：`PUSH` 里加了 `code/Desktop/racecar`，远端那一半
是 `colcon build --packages-select racecar nav2_waypoint_cycle --parallel-workers 6`，
实测 `--only racecar-build` → 本地 PASS 0.0 s、远端 PASS 2.9 s（整树现在编不过，第 17 条，
所以清单里点着名只编这两个包）。同时加了 `--only <key>` 好单跑一行。

**还开着的是本地那一半**：Windows 上编不了 colcon，写的是 `racecar_ctl.py where`（入口还在解析、
后端是 `shim`）—— 它**证明不了「那 17 个脚本都能加载」**，而后者才是这台机器上真会挂的地方
（证据 77 里三个问题全是"编译过、跑起来才炸"）。**成本**：把「17 个脚本在 `shim_run.py` 下都能
import / 加载」做成一个仓库级工具 —— 证据 77 那套一次性脚本的输出还在
`.scratch/waypoint_file.out`（`loaded 17/17 scripts under the shim`），工具就是把它固化成一条命令，
挂到本地那一半上。

### 19. `NavigateThroughPoses` 的 goal 字段，兼容层和车上不是一个定义（2026-09-19 新开）

这是第一次在车上 `ros2 run racecar go.py sim_room12.csv` 当场崩掉的东西：

```text
AttributeError: 'list' object has no attribute 'goals'
```

不是脚本写错，是同一个 action 在两边不是同一个定义：兼容层（移植的 nav2 1.5.1）的
`NavigateThroughPoses.action` 里 goal 是 `nav_msgs/Goals poses`（航点在它的 `.goals` 里），
车上那份 Jazzy 是 `geometry_msgs/PoseStamped[] poses`（直接就是数组）。于是 10 个脚本
（`detct.py` `end_3_12.py` `go.py` `goal_l.py` `go一条路径.py` `一条路径+csv.py`
`人工智能赛道初始版本代码.py` `第二个点nav2打断，然后转圈圈.py` `终点不停.py` `跑一圈以内.py`）
写的 `goal_msg.poses.goals = poses` **只可能在兼容层上对**。

现在 10 个都改成两分支（`hasattr(goal_msg.poses, "goals")` 走前者，否则 `goal_msg.poses = poses`），
两边各量了一次：车上从「构造 goal 就崩」变成「起来等服务器」（无 traceback），兼容层上
`end_3_12.py` 与 `终点不停.py` 各 66 s、`error_code=0`、9/9 `waypoint_status=1`。探测脚本
`.scratch/probe_ntp_poses.py`（两边跑同一份）、实测与门禁全貌见证据 82。

**没改兼容层那边**：`nav_msgs/Goals` 是移植这一版 nav2 自己的定义（`nav2_extracted` 的 `.action`
就这么写，C++ 侧 `navigate_through_poses.cpp` 也按它读），改回 Jazzy 那版就不是移植了。
**仍然开着的一半**：车上只测了 `go.py` 一个；另外 9 个各自还有 `/goal_pose`、`Trigger`、
分组推进那些分支，在这台设备上一次都没走过。
