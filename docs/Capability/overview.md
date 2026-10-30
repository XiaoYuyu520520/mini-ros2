---
layout: default
title: 5 能力清单 · 已实现能力
---

# 5.1 已实现能力

<div class="overview">
<div class="label">本页总览</div>
<p>这一页列的是<strong>实测跑过</strong>的能力，每条都带数字。还没做、或者只做到一半的，
见 <a href="../FAQ/limitations">功能限制与缺口</a>。</p>
</div>

## 5.1.1 一览

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
| Windows 可视化（`viz`，命令名 `rviz2`） | ✅ 地图 / 扫描 / 路径 / 位姿端到端画出来；发目标点与初始位姿真窗口验过 |
| 自主回充 / 路网（docking / route） | ⚠️ 能拉到 ACTIVE，**没有 dock 实例、没有图文件** |

## 5.1.2 建图 SLAM

`slam_toolbox` 2.8.5 移植到 Windows（sync / async / localization / merge_maps 四个 exe，
`/slam_map`），跟踪误差 **0.000 m / 0.0°**。

| 场景 | 实测 |
| --- | --- |
| **活的 SLAM 一整圈** | 车走一条闭合的圈，八个 action 全 `SUCCEEDED`；`473 x 473` 的图三种拿法（`map_dump.exe` / `map_saver_cli.exe --fmt pgm` / `--fmt png`）像素一致，经 `map_server` 往返与源文件 **0 处不同** |
| **位姿图存档** | `serialize_map` / `deserialize_map` 验过：`-p map_file_name:=` 与 `ros2 service call slam_toolbox/deserialize_map` 两个入口都能把存下来的位姿图读回来 |
| **定位模式** | 同一份存档换个进程读回来，`map_start_at_dock:=true` 与 `map_start_pose:=[0.0,0.0,0.0]` 给出**逐位相同**的 `472x473 / occupied=338`；不给初始位姿则是「节点照跑、`/map` 照发、图是空的」（`occupied=0`） |
| **走位建图** | 走一条 4.5 m 见方的环线建出整间 12 m 房 —— `233x233`、free **82.8%** / unknown 14.9% / occupied **2.4%**（1286 格） |

走位建图要改 `slam_toolbox` 的三个运动门槛（`0.5 / 0.5 / 0.5` → `0.1`），
原因见 <a href="../Basic_application/slam">3.3 SLAM 建图</a>。

## 5.1.3 定位 Localization

`nav2_amcl` 1.5.1（粒子滤波 + 光束 / 似然场传感器模型）移植到 Windows，
`amcl_bridge_node` 发 `map -> odom`。`use_sim_time` 打开时节点时间戳跟随机器人的仿真时钟。

## 5.1.4 全局规划 Global planning

| 规划器 | 状态 |
| --- | --- |
| **NavFn** | ✅ `nav2_navfn_planner`，经典的图搜索 |
| **Smac Hybrid-A\*** | ✅ 自实现的 OMPL 子集；**在「直线不通」的目标上真绕过障碍** —— 直线 5.01 m 的目标，规划器给出来的是 8.07 m 的绕行路线（自己独立算的测地线 7.82 m），车实际走了 8.5~8.8 m、最小离障 0.283 m、`/plan` x44、`/cmd_vel` x467、47.0 s、`SUCCEEDED` |

<div class="callout note">
<span class="label">修过的一个上游 bug</span>
<p>SMAC 的 <code>std::bad_alloc</code>（第二次起每次 47 ms 抛）根因是
<strong>角度箱越界</strong>，不是启发式缓存也不是膨胀半径。用 gdb 的
<code>__cxa_throw</code> catchpoint 钉住分配点，四处越界全在 <code>nav2_smac_planner/</code> 里
（<code>analytic_expansion_impl.hpp</code> 的角度归一化、
<code>collision_checker.cpp</code> 的 <code>oriented_footprints_[angle_bin]</code> 下标、
<code>node_hybrid.cpp</code> 的 <code>delta_xs</code> 下标）。</p>
<p>修后同一个坏目标稳定报 <code>PlannerTimedOut: "exceeded maximum iterations"</code> 而不是崩，
<code>bad_alloc</code> 行数从「每次规划都有」变成 **0**。两个当时设的 workaround
（<code>cache_obstacle_heuristic: False</code>、<code>inflation_radius: 0.45</code>）
<strong>都建立在错因上</strong>，已回到上游与真车的取值。</p>
</div>

## 5.1.5 局部控制 Local control

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

## 5.1.6 自救行为 Behaviors

`nav2_behaviors` 的 `behavior_server`：`Spin` / `BackUp` / `DriveOnHeading` / `Wait` /
`AssistedTeleop` 五个插件全部创建并激活，`spin` / `wait` / `backup` 各真跑过一次。

## 5.1.7 路径平滑 / 多点 Smoothing & waypoints

`smoother_server` + `waypoint_follower` 被同一个 `lifecycle_manager` 拉到 ACTIVE，
并由 `nav2_waypoints.exe` 真正驱动：`ComputePathToPose` → `SmoothPath` → `FollowWaypoints`
连跑 6 次全 `SUCCEEDED`、`missed_waypoints` 全 0，**最尖的拐角 45.0° → 4.9°**。

`navigate_through_poses` 的三航点也端到端跑过：`SUCCEEDED` / 47.8 s / 0 次恢复。

## 5.1.8 安全链 Safety chain

`nav2_velocity_smoother` + `nav2_collision_monitor`，两种接法：

| 接法 | 效果 |
| --- | --- |
| `nav2_safety.bat`（**旁路**） | `/cmd_vel` → `/cmd_vel_smoothed` → `/cmd_vel_monitored`，两条输出都没有第二个写者、**不动车**。用来量测与观察 |
| `nav2_stack.bat --safety`（**进链**） | `controller_server` / `behavior_server` 被 remap 到 `/cmd_vel_nav`，smoother 跟过去，`collision_monitor` 的 `cmd_vel_out_topic` 指回 `/cmd_vel`，成为它的**唯一写者** |

**进链的实测**：把停止多边形放大到 12 m 时，同一个目标从「走 2.39 m」变成「**走 0.00 m**」，
而控制器一直在发 0.45 m/s —— 这一层**就是车的命运**。

`/collision_monitor_state` 已经进了编解码表，`ros2 topic echo` 打得出
`action_type: 2 (slowdown)` 与 `polygon_name: "StopPolygon"`。

## 5.1.9 行为树 Behavior tree

`nav2_behavior_tree` 节点注册表 + `bt_navigator`（`navigate_to_pose` / `navigate_through_poses`
两棵树）。行为树是上游 XML 的副本（`nav2/ros2_shim/config/bt/*.xml`），末尾保留着恢复轮盘。

<div class="callout note">
<span class="label">必须记住的约束</span>
<p>行为树是<strong>激活时</strong>建的，叶子对应的 action server 不在，
整个 <code>bt_navigator</code> 就激活不了 —— 所以 <code>lifecycle_manager</code> 的
<code>node_names</code> <strong>必须列上 <code>behavior_server</code></strong>。</p>
</div>

## 5.1.10 Costmap

static / obstacle / voxel / inflation / keepout_filter / speed_filter **全部激活成功**。

## 5.1.11 地图 Map

`nav2_map_server` + PGM / PNG / YAML 读写（绕开 GraphicsMagick）、`map_saver_cli.exe`、内存图；
存下来的图由 `map_server.exe` 读回来，与源文件逐字节相同。

## 5.1.12 时间 Time

`use_sim_time` + `/clock`：Ubuntu 仿真把消息头的时间戳写成**仿真时间**（从 0 开始），
`/clock` 也是它，由守护进程镜像进共享内存。Windows 侧节点打开 `use_sim_time` 后 `now()`
跟随 `/clock`。

<div class="callout warn">
<span class="label">注意</span>
<p><strong>没打开 <code>use_sim_time</code> 的节点</strong>，<code>now()</code> 是系统墙钟
（1.79e9 s），与消息戳差 56 年 —— 这类节点里凡是把 <code>now()</code> 和消息戳配在一起的代码
都在向未来外推。</p>
</div>

## 5.1.13 跨机 Cross-machine

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

## 5.1.14 参数 Parameters

节点自己名字下的**六个参数服务**：`list` / `get` / `set` / `describe` / `dump` 对着**正在跑**的
节点读写（`set` 会走 nav2 自己的校验回调，非法值被拒且节点日志留痕）。

`/parameter_events` 由节点自己发（`declare_parameter` → `new_parameters`，应用成功的 `set`
→ `changed_parameters`），**另一个进程不用事先知道任何服务名就能看参数变化**；
整条栈起来时图里 14 个 publisher，多个进程共用一个环形缓冲。

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="../Architecture/action">4.3 Action 实现</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="validation">5.2 验收与实测数据</a></div>
</div>
