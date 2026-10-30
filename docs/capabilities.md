---
layout: default
title: 能力清单 Capabilities
---

# 能力清单 Capabilities

> **[中文](#中文) · [English](#english)**

---

# 中文

这一页列的是**实测跑过**的能力，每条都带数字。还没做、或者只做到一半的，见
[限制与缺口](limitations.html)。

## 一览

| 能力 | 状态 |
| --- | --- |
| 上游 nav2 服务器本体（AMCL + planner + controller + bt_navigator + behavior_server + smoother_server + waypoint_follower + lifecycle_manager）一条命令拉起并闭环 | ✅ 通过 |
| 航点导航：`ComputePathToPose` → `SmoothPath` → `FollowWaypoints` 一条链 | ✅ 连跑 6 次全部 `SUCCEEDED`，`missed_waypoints` 全 0 |
| 行为树末尾的恢复轮盘 `Spin` / `Wait` / `BackUp` | ✅ 真跑过一次，驱动了小车（倒车 0.31 m） |
| 速度平滑 + 碰撞监控 | ✅ 旁路与进链两种接法都验过 |
| 跨机 action / service 编解码 | ✅ 20 个 action + 38 种 service 类型 |
| 局部控制器：DWB / MPPI / Graceful / RPP | ✅ 同一个栈里并存，四个都能被 `/controller_selector` 选中 |
| 地图存取：PGM + 8 位 PNG | ✅ `map_saver_cli.exe` 存 PGM / PNG + YAML，`map_server.exe` 读回来 |
| 活的 SLAM 一整圈 | ✅ 车走闭合的圈，八个 action 全 `SUCCEEDED` |
| 参数事件流 `/parameter_events` | ✅ 节点自己发，另一个进程读得到 |
| 运行期调参：参数服务 + `ros2 param` | ✅ 对着正在跑的节点读写 |
| `vector_object_server` | ✅ 被推到 ACTIVE、发出 `/vo_map` 之后干净退出 |
| 自主回充 / 路网（docking / route） | ⚠️ 能拉到 ACTIVE，**没有 dock 实例、没有图文件** |

## 建图 SLAM

`slam_toolbox` 2.8.5 移植到 Windows（sync / async / localization / merge_maps 四个 exe，
`/slam_map`），跟踪误差 **0.000 m / 0.0°**。

* **活的 SLAM 一整圈**：车走一条闭合的圈，八个 action 全 `SUCCEEDED`；
  `473 x 473` 的图三种拿法（`map_dump.exe` / `map_saver_cli.exe --fmt pgm` /
  `--fmt png`）像素一致，经 `map_server` 往返与源文件 **0 处不同**。
* **位姿图存档**（`serialize_map` / `deserialize_map`）已验过：`-p map_file_name:=` 与
  `ros2 service call slam_toolbox/deserialize_map` 两个入口都能把存下来的位姿图读回来。
* **定位模式**（`--mode localization` + 初始位姿）也压过：同一份存档换个进程读回来，
  `map_start_at_dock:=true` 与 `map_start_pose:=[0.0,0.0,0.0]` 给出**逐位相同**的
  `472x473 / occupied=338`；不给初始位姿则是「节点照跑、`/map` 照发、图是空的」
  （`occupied=0`）。
* **走位建图**：走一条 4.5 m 见方的环线建出整间 12 m 房 —— `233x233`、
  free **82.8%** / unknown 14.9% / occupied **2.4%**（1286 格）。
  （`slam_toolbox` 的三个运动门槛 `minimum_travel_distance` / `minimum_travel_heading` /
  `minimum_time_interval` 出厂是 `0.5 / 0.5 / 0.5`，车停着不动整个跑车只提交 1 张扫描，
  墙只被一张扫描打到、`hitCnt = 1`，图就建不出来。降到 `0.1` 之后整间房出来了。）

**缺的周边**：多张子图的**合并语义**（`merge_maps_kinematic.exe` 的两个服务真调过，但
`merge_submaps` 要靠 interactive marker 的反馈来摆放子图，这套 shim 里没有 rviz 那样的
反馈源）；`slam_toolbox` 自带的 `map_saver.cpp` 走 `system("ros2 run ...")`，Windows 上必失败
（用 `map_saver_cli.exe`）。

## 定位 Localization

`nav2_amcl` 1.5.1（粒子滤波 + 光束 / 似然场传感器模型）移植到 Windows，
`amcl_bridge_node` 发 `map -> odom`。`use_sim_time` 打开时节点时间戳跟随机器人的仿真时钟。

## 全局规划 Global planning

| 规划器 | 状态 |
| --- | --- |
| **NavFn** | ✅ `nav2_navfn_planner`，经典的图搜索 |
| **Smac Hybrid-A\*** | ✅ 自实现的 OMPL 子集；**在「直线不通」的目标上真绕过障碍** —— 直线 5.01 m 的目标，规划器给出来的是 8.07 m 的绕行路线（自己独立算的测地线 7.82 m），车实际走了 8.5~8.8 m、最小离障 0.283 m、`/plan` x44、`/cmd_vel` x467、47.0 s、`SUCCEEDED` |

> **修过的一个上游 bug**：SMAC 的 `std::bad_alloc`（第二次起每次 47 ms 抛）根因是
> **角度箱越界**，不是启发式缓存也不是膨胀半径。用 gdb 的 `__cxa_throw` catchpoint 钉住
> 分配点，四处越界全在 `nav2_smac_planner/` 里（`analytic_expansion_impl.hpp` 的角度归一化、
> `collision_checker.cpp` 的 `oriented_footprints_[angle_bin]` 下标、`node_hybrid.cpp` 的
> `delta_xs` 下标）。修后同一个坏目标稳定报 `PlannerTimedOut: "exceeded maximum iterations"`
> 而不是崩，`bad_alloc` 行数从「每次规划都有」变成 **0**。两个当时设的 workaround
> （`cache_obstacle_heuristic: False`、`inflation_radius: 0.45`）**都建立在错因上**，已回到上游
> 与真车的取值。

## 局部控制 Local control

同一个栈里并存四个能被 `/controller_selector` 选中的控制器：

```yaml
controller_plugins: ["FollowPath", "DWBFollowPath", "MPPIFollowPath", "GracefulFollowPath"]
```

启动日志里四个都打印了 `Created controller`，装载与激活都验过，都能在**运行期**切过去
驱动小车（MPPI 是上游默认的那个）：

| 控制器 | 内容 |
| --- | --- |
| `FollowPath` | `nav2_regulated_pure_pursuit_controller`（RPP，默认） |
| `DWBFollowPath` | `nav2_dwb_controller`（`dwb_core` + `dwb_plugins` + `dwb_critics`） |
| `MPPIFollowPath` | `nav2_mppi_controller`（`MPPIController` + `DiffDriveMotionModel` + 8 个 critic + `DefaultOptimalTrajectoryValidator`） |
| `GracefulFollowPath` | `nav2_graceful_controller` |

还有 `nav2_rotation_shim_controller`（先把车原地转向，再交给内部的 RPP）也编好了，
但它占的就是 `FollowPath` 这个 id，要用自己那份参数文件起栈（`--no-overrides`）。

## 自救行为 Behaviors

`nav2_behaviors` 的 `behavior_server`：`Spin` / `BackUp` / `DriveOnHeading` / `Wait` /
`AssistedTeleop` 五个插件全部创建并激活，`spin` / `wait` / `backup` 各真跑过一次。

## 路径平滑 / 多点 Smoothing & waypoints

`smoother_server` + `waypoint_follower` 被同一个 `lifecycle_manager` 拉到 ACTIVE，
并由 `nav2_waypoints.exe` 真正驱动：`ComputePathToPose` → `SmoothPath` → `FollowWaypoints`
连跑 6 次全 `SUCCEEDED`、`missed_waypoints` 全 0，**最尖的拐角 45.0° → 4.9°**。

`navigate_through_poses` 的三航点也端到端跑过：`SUCCEEDED` / 47.8 s / 0 次恢复。

## 安全链 Safety chain

`nav2_velocity_smoother` + `nav2_collision_monitor`，两种接法：

| 接法 | 效果 |
| --- | --- |
| `nav2_safety.bat`（**旁路**） | `/cmd_vel` → `/cmd_vel_smoothed` → `/cmd_vel_monitored`，两条输出都没有第二个写者、**不动车**。用来量测与观察 |
| `nav2_stack.bat --safety`（**进链**） | `controller_server` / `behavior_server` 被 remap 到 `/cmd_vel_nav`，smoother 跟过去，`collision_monitor` 的 `cmd_vel_out_topic` 指回 `/cmd_vel`，成为它的**唯一写者** |

**进链的实测**：把停止多边形放大到 12 m 时，同一个目标从「走 2.39 m」变成「**走 0.00 m**」，
而控制器一直在发 0.45 m/s —— 这一层**就是车的命运**。

`/collision_monitor_state`（`nav2_msgs/msg/CollisionMonitorState`）**已经进了编解码表**，
`ros2 topic echo` 打得出 `action_type: 2 (slowdown)` 与 `polygon_name: "StopPolygon"`。

## 行为树 Behavior tree

`nav2_behavior_tree` 节点注册表 + `bt_navigator`（`navigate_to_pose` / `navigate_through_poses`
两棵树）。行为树是上游 XML 的副本（`nav2/ros2_shim/config/bt/*.xml`），末尾保留着恢复轮盘。

> **一个必须记住的约束**：行为树是**激活时**建的，叶子对应的 action server 不在，
> 整个 `bt_navigator` 就激活不了 —— 所以 `lifecycle_manager` 的 `node_names` **必须列上
> `behavior_server`**。

## Costmap

static / obstacle / voxel / inflation / keepout_filter / speed_filter **全部激活成功**。

## 地图 Map

`nav2_map_server` + PGM / PNG / YAML 读写（绕开 GraphicsMagick）、`map_saver_cli.exe`、内存图；
存下来的图由 `map_server.exe` 读回来，与源文件逐字节相同。

图像格式是**自己实现的两个 coder**（`include/Magick++.h` 替掉上游对 GraphicsMagick 的依赖）：

* **PGM**：进 P5 / P2，出 P5
* **8 位 PNG**：认灰度 / RGB / RGBA / 灰度+alpha / 调色板五种颜色类型和五种行过滤器，
  出灰度或灰度+alpha，解压用工具链自带的 zlib

`mode: scale` 要 PNG 才存得下透明通道 —— 上游 `checkSaveParameters()` 在那个模式下默认就是
`png`，`slam_toolbox` 自带的配置里 `image_format` 也是 `png`。

## 时间 Time

`use_sim_time` + `/clock`：Ubuntu 仿真把消息头的时间戳写成**仿真时间**（从 0 开始），
`/clock` 也是它，由守护进程镜像进共享内存。Windows 侧节点打开 `use_sim_time` 后 `now()`
跟随 `/clock`。

> **没打开 `use_sim_time` 的节点**，`now()` 是系统墙钟（1.79e9 s），与消息戳差 56 年 ——
> 这类节点里凡是把 `now()` 和消息戳配在一起的代码都在向未来外推。

## 跨机 Cross-machine

| 面 | 覆盖 |
| --- | --- |
| 话题 | **40 种**类型的 CDR ↔ 共享内存双向转码，逐字节精确 |
| service | **38 种**类型（`slam_toolbox` 的全部 11 个 srv、`nav2_msgs` 的全部 20 个 srv、`nav_msgs/srv/GetMap`、`lifecycle_msgs` 的 3 个、`std_srvs` 的 3 个） |
| action | **20 个**（仿真自己的 `Drive` + `nav2_msgs` 里**每一个 `.action` 文件**的 19 个），每一条都带 Goal / Result 的逐字段打印器 |
| `/clock`、`/tf` / `/tf_static` | ✅ |

**转码正确性**：`bridge/tests/generate_fixtures.py` 从兼容层头文件里**解析出 307 个结构体布局**
（独立于 C++ 的第二实现），用 `rosbags`（ROS2 Jazzy 的 CDR 规则）生成样本消息。
`bridge_cdr_check` 断言 `cdr_to_shim(cdr) == shim`、`shim_to_cdr(shim) == cdr` 以及往返 ——
**35 个 fixture、105 组字节比较、0 失败**。

## 参数 Parameters

节点自己名字下的**六个参数服务**：`list` / `get` / `set` / `describe` / `dump` 对着**正在跑**的
节点读写（`set` 会走 nav2 自己的校验回调，非法值被拒且节点日志留痕）。

`/parameter_events` 由节点自己发（`declare_parameter` → `new_parameters`，应用成功的 `set`
→ `changed_parameters`），**另一个进程不用事先知道任何服务名就能看参数变化**；
整条栈起来时图里 14 个 publisher，多个进程共用一个环形缓冲。

---

# English

Everything on this page has actually been run, with numbers. What is missing — or only half done —
is on [Limitations](limitations.html).

## At a glance

| Capability | Status |
| --- | --- |
| The upstream nav2 servers themselves (AMCL + planner + controller + bt_navigator + behavior_server + smoother_server + waypoint_follower + lifecycle_manager) brought up in one command and closed | ✅ pass |
| Waypoint navigation: `ComputePathToPose` → `SmoothPath` → `FollowWaypoints` as one chain | ✅ 6/6 `SUCCEEDED`, `missed_waypoints` all 0 |
| The recovery wheel at the end of the behavior tree (`Spin` / `Wait` / `BackUp`) | ✅ really ran once and drove the car (0.31 m reverse) |
| Velocity smoothing + collision monitoring | ✅ both the bypass and the in-chain wiring verified |
| Cross-machine action / service transcoding | ✅ 20 actions + 38 service types |
| Local controllers: DWB / MPPI / Graceful / RPP | ✅ four coexist in one stack, all selectable via `/controller_selector` |
| Map I/O: PGM + 8-bit PNG | ✅ `map_saver_cli.exe` writes PGM / PNG + YAML, `map_server.exe` reads it back |
| A live SLAM lap | ✅ the car drove a closed loop, all eight actions `SUCCEEDED` |
| Parameter event stream `/parameter_events` | ✅ published by the node itself, readable by another process |
| Runtime tuning: parameter services + `ros2 param` | ✅ reads and writes a *running* node |
| `vector_object_server` | ✅ pushed to ACTIVE, emitted `/vo_map`, exited cleanly |
| Autonomous docking / route graph | ⚠️ reaches ACTIVE, but **no dock instance and no graph file** |

## Mapping (SLAM)

`slam_toolbox` 2.8.5 ported to Windows (sync / async / localization / merge_maps executables on
`/slam_map`), tracking error **0.000 m / 0.0°**.

* **A live lap**: the car drove a closed loop, all eight actions `SUCCEEDED`; the `473 x 473`
  map is pixel-identical across three ways of getting it (`map_dump.exe`,
  `map_saver_cli.exe --fmt pgm`, `--fmt png`) and **byte-identical** to the source after a
  `map_server` round trip — 0 differing bytes.
* **Pose-graph archiving** (`serialize_map` / `deserialize_map`) is verified: both
  `-p map_file_name:=` and `ros2 service call slam_toolbox/deserialize_map` read an archived
  pose graph back.
* **Localization mode** (`--mode localization` + an initial pose) is verified too: the same archive
  read back by a different process gives a **bit-identical** `472x473 / occupied=338` for both
  `map_start_at_dock:=true` and `map_start_pose:=[0.0,0.0,0.0]`; with no initial pose the node
  runs, `/map` is published, and the map is empty (`occupied=0`).
* **Mapping by driving**: a 4.5 m square loop maps a whole 12 m room — `233x233`,
  free **82.8%** / unknown 14.9% / occupied **2.4%** (1286 cells). (slam_toolbox's three motion
  thresholds `minimum_travel_distance` / `minimum_travel_heading` / `minimum_time_interval` ship
  as `0.5 / 0.5 / 0.5`; a parked car submits a single scan for a whole run, each wall cell is hit
  by exactly one scan (`hitCnt = 1`), and no wall appears. At `0.1` the whole room appears.)

**Missing around the edges**: the **merge semantics** of multiple submaps (the two services of
`merge_maps_kinematic.exe` were really called, but `merge_submaps` relies on interactive-marker
feedback to place submaps, and this shim has no rviz-like feedback source); slam_toolbox's own
`map_saver.cpp` goes through `system("ros2 run ...")`, which always fails on Windows (use
`map_saver_cli.exe`).

## Localization

`nav2_amcl` 1.5.1 (particle filter with beam and likelihood-field sensor models) ported to
Windows; `amcl_bridge_node` publishes `map -> odom`. With `use_sim_time` on, node timestamps follow
the robot's simulation clock.

## Global planning

| Planner | Status |
| --- | --- |
| **NavFn** | ✅ `nav2_navfn_planner`, the classic graph search |
| **Smac Hybrid-A\*** | ✅ a self-implemented OMPL subset; it **really goes around an obstacle** for a goal the straight line cannot reach — the goal 5.01 m away in a straight line produced an 8.07 m detour (the independently computed geodesic is 7.82 m); the car actually drove 8.5–8.8 m, minimum obstacle clearance 0.283 m, `/plan` ×44, `/cmd_vel` ×467, 47.0 s, `SUCCEEDED` |

> **An upstream bug that was fixed**: SMAC's `std::bad_alloc` (thrown in 47 ms from the second
> planning attempt onward) was caused by **an out-of-range angle bin** — not the heuristic cache
> and not the inflation radius. A gdb `__cxa_throw` catchpoint pinned the allocation site; all four
> out-of-range spots are in `nav2_smac_planner/` (the angle normalisation in
> `analytic_expansion_impl.hpp`, the `oriented_footprints_[angle_bin]` index in
> `collision_checker.cpp`, the `delta_xs` index in `node_hybrid.cpp`). After the fix the same bad
> goal reliably reports `PlannerTimedOut: "exceeded maximum iterations"` instead of crashing, and
> the `bad_alloc` line count went from "every planning attempt" to **0**. Both workarounds that
> had been put in place (`cache_obstacle_heuristic: False`, `inflation_radius: 0.45`) **were based
> on the wrong cause** and have been reverted to the upstream / real-car values.

## Local control

Four controllers coexist in one stack and are selectable via `/controller_selector`:

```yaml
controller_plugins: ["FollowPath", "DWBFollowPath", "MPPIFollowPath", "GracefulFollowPath"]
```

All four print `Created controller` at startup, loading and activation are verified, and all four
can be switched to at **runtime** to drive the car (MPPI is upstream's default):

| Controller | Contents |
| --- | --- |
| `FollowPath` | `nav2_regulated_pure_pursuit_controller` (RPP, default) |
| `DWBFollowPath` | `nav2_dwb_controller` (`dwb_core` + `dwb_plugins` + `dwb_critics`) |
| `MPPIFollowPath` | `nav2_mppi_controller` (`MPPIController` + `DiffDriveMotionModel` + 8 critics + `DefaultOptimalTrajectoryValidator`) |
| `GracefulFollowPath` | `nav2_graceful_controller` |

`nav2_rotation_shim_controller` (turn the car in place, then hand over to the inner RPP) is also
built, but it occupies the `FollowPath` id, so it needs its own parameter file to start the stack
(`--no-overrides`).

## Behaviors

`nav2_behaviors`' `behavior_server`: all five plugins — `Spin` / `BackUp` / `DriveOnHeading` /
`Wait` / `AssistedTeleop` — are created and activated, and `spin` / `wait` / `backup` each really
ran once.

## Smoothing / waypoints

`smoother_server` + `waypoint_follower` are pushed to ACTIVE by the same `lifecycle_manager` and
really driven by `nav2_waypoints.exe`: `ComputePathToPose` → `SmoothPath` → `FollowWaypoints` ran
6 times, all `SUCCEEDED`, `missed_waypoints` all 0, and the **sharpest corner went from 45.0° to
4.9°**.

The three-waypoint `navigate_through_poses` also ran end to end: `SUCCEEDED` / 47.8 s / 0 recoveries.

## Safety chain

`nav2_velocity_smoother` + `nav2_collision_monitor`, two wirings:

| Wiring | Effect |
| --- | --- |
| `nav2_safety.bat` (**bypass**) | `/cmd_vel` → `/cmd_vel_smoothed` → `/cmd_vel_monitored`; neither output has a second writer and **the car does not move**. For measuring and observing |
| `nav2_stack.bat --safety` (**in chain**) | `controller_server` / `behavior_server` are remapped to `/cmd_vel_nav`, the smoother follows, and `collision_monitor`'s `cmd_vel_out_topic` points back at `/cmd_vel`, making it the **only writer** |

**Measured in-chain**: enlarging the stop polygon to 12 m changed the same goal from "drove 2.39 m"
to "**drove 0.00 m**", while the controller kept commanding 0.45 m/s — this layer **is** the car's
fate.

`/collision_monitor_state` (`nav2_msgs/msg/CollisionMonitorState`) **is in the codec table**, and
`ros2 topic echo` prints `action_type: 2 (slowdown)` and `polygon_name: "StopPolygon"`.

## Behavior tree

`nav2_behavior_tree` node registry + `bt_navigator` (the `navigate_to_pose` and
`navigate_through_poses` trees). The trees are copies of the upstream XML
(`nav2/ros2_shim/config/bt/*.xml`), with the recovery wheel kept at the end.

> **A constraint worth remembering**: the behavior tree is **built at activation**, and if the
> action server behind a leaf is missing, the whole `bt_navigator` cannot activate — so
> `lifecycle_manager`'s `node_names` **must list `behavior_server`**.

## Costmap

static / obstacle / voxel / inflation / keepout_filter / speed_filter **all activate successfully**.

## Map

`nav2_map_server` + PGM / PNG / YAML I/O (bypassing GraphicsMagick), `map_saver_cli.exe`, the
in-memory map; a saved map read back by `map_server.exe` is byte-identical to the source.

The image formats are **two hand-written coders** (`include/Magick++.h` replaces upstream's
GraphicsMagick dependency):

* **PGM**: reads P5 / P2, writes P5
* **8-bit PNG**: reads grayscale / RGB / RGBA / grayscale+alpha / palette colour types and five row
  filters, writes grayscale or grayscale+alpha, decompressing with the toolchain's own zlib

`mode: scale` needs PNG to keep the alpha channel — upstream's `checkSaveParameters()` already
defaults to `png` in that mode, and slam_toolbox's own config also says `image_format: png`.

## Time

`use_sim_time` + `/clock`: the Ubuntu simulator stamps messages with **simulation time** (starting
at 0) and `/clock` is the same, mirrored into shared memory by the daemon. With `use_sim_time` on,
`now()` on the Windows side follows `/clock`.

> **For nodes without `use_sim_time`**, `now()` is the system wall clock (1.79e9 s) — 56 years away
> from the message stamps. Any code in such a node that pairs `now()` with a message stamp is
> extrapolating into the future.

## Cross-machine

| Surface | Coverage |
| --- | --- |
| Topics | **40 types** transcoded both ways between CDR and shared memory, byte-exact |
| Services | **38 types** (all 11 slam_toolbox srv, all 20 `nav2_msgs` srv, `nav_msgs/srv/GetMap`, 3 `lifecycle_msgs`, 3 `std_srvs`) |
| Actions | **20** (the simulator's own `Drive` + **every one** of the 19 `.action` files in `nav2_msgs`), each with a per-field Goal / Result printer |
| `/clock`, `/tf` / `/tf_static` | ✅ |

**Transcoding correctness**: `bridge/tests/generate_fixtures.py` **parses 307 struct layouts out of
the compatibility-layer headers** (an implementation independent of the C++ one) and uses `rosbags`
(ROS2 Jazzy's CDR rules) to generate sample messages. `bridge_cdr_check` asserts
`cdr_to_shim(cdr) == shim`, `shim_to_cdr(shim) == cdr` and the round trip —
**35 fixtures, 105 byte comparisons, 0 failures**.

## Parameters

The **six parameter services** under a node's own name: `list` / `get` / `set` / `describe` / `dump`
read and write a *running* node (`set` goes through nav2's own validation callback, so invalid
values are rejected and leave a line in the node log).

`/parameter_events` is published by the node itself (`declare_parameter` → `new_parameters`, an
applied `set` → `changed_parameters`), so **another process can watch parameter changes without
knowing any service name in advance**; with the whole stack up there are 14 publishers in the
graph, sharing one ring buffer.
