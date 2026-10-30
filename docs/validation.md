---
layout: default
title: 验收与证据 Validation
---

# 验收与证据 Validation

> **[中文](#中文) · [English](#english)**

---

# 中文

## 验收结论

| # | 验收标注 | 判定 | 结论 |
| --- | --- | --- | --- |
| 1 | gRPC SDK 快速扫描建立持久连接；一个 bat 临时加载环境后即可用 ros2，`ros2 topic list` 等保持一致 | 冷启动后 `ros2.bat topic list -t` 返回 9 个话题且类型全部正确；耗时随守护进程起多快而变（最近一次冷启动 1.88 s，现场那次 1.31 s，机器当时不空），但结果不再随之变化 | **通过** |
| 2 | Windows 发布话题，Ubuntu 端也能正常订阅 | Windows 发 `/cmd_vel`，Ubuntu 小车位移 **0.81 m**，`/odom.twist.linear.x` = 0.250 | **通过** |
| 3 | nav2 正常订阅话题建图、规划路径、指挥小车移动 | 480×480 costmap（13728 lethal）、NavFn 规划 287 点 / 15.34 m、23.0 s 行驶 **11.73 m**，距目标 **0.26 m** | **通过** |
| 4 | 移植 `nav2_amcl` + `slam_toolbox`（humble 分支）到 Windows：静态编译、运行时不依赖 MSYS | 移植产物 `objdump -p` 里只剩 `KERNEL32.dll` + `api-ms-win-crt-*`；`PATH` 只留 `C:\Windows\System32;C:\Windows`、`ROS2_BRIDGE_MSYS2` 指向不存在的目录时，守护进程照常起、`ros2.bat topic list -t` 与上行发布都正常 | **通过** |
| 5 | 规划器**插件层**：`nav2_smac_planner`（Hybrid-A*）接入，规划器从 NavFn 换成 Smac 后整栈照常建图、规划、闭环 | 连跑三次 `nav2_stack.bat --params <Smac>` 全部 `SUCCEEDED`：行驶 **2.36 / 2.79 / 2.44 m**，`/plan` 2 / 17 / 6 条，`/cmd_vel` 118 / 590 / 133 条；`planner_server` 日志里 `Created global planner plugin GridBased of type nav2_smac_planner::SmacPlannerHybrid` → `Using motion model: Dubin` → `Managed nodes are active` | **通过** |
| 6 | 共享内存通道的几何是有代价的：`map`/`costmap` 类话题不再按 64 MB × 8 槽预留 | `planner_server` 常驻 **6191 MB → 231 MB**、`bt_navigator` 1277 MB → 319 MB，空闲内存 0.29 GB → 3.18 GB；改前 Smac 那一栈起不来（`bt_navigator` 里一个 `std::bad_alloc`），改后连跑三次全过 | **通过** |

## 一键复现

```bat
cd E:\DesktopData\app\code\CPU
python bridge\scripts\acceptance_check.py
```

最近一次运行（`bridge/docs/validation/acceptance-report.json`）：

```
=== topic_list: Windows lists the Ubuntu topics (cold start)
[PASS] topic_list (1.9 s) - 9 topics with types in 2.0 s (cold start)
=== topic_info: Windows resolves /scan to the gRPC peer
[PASS] topic_info (0.2 s) - reported as remote (bridged over gRPC)
=== scan_rate: Mirrored /scan keeps its 10 Hz
[PASS] scan_rate (6.2 s) - 9.64 Hz average over 55 samples
=== uplink: Windows /cmd_vel moves the Ubuntu robot
[PASS] uplink (4.7 s) - moved 0.81 m, /odom twist.linear.x=0.250 while commanded
=== uplink_nav2: the plan nav2 publishes reaches the robot (default uplink table)
[PASS] uplink_nav2 (11.6 s) - default table armed /plan, /behavior_tree_log;
       /plan forwarded 14/20 msgs, failed=0
=== cmd_vel_watchdog: a /cmd_vel that stops arriving stops the robot
[PASS] cmd_vel_watchdog (4.6 s) - drove 0.37 m on a 1 s command, then 0.000 m
       in the next second, /odom twist.linear.x=0
=== nav2_demo: nav2 plans a path and drives to the goal
[PASS] nav2_demo (22.8 s) - goal [-8.775, -6.375] reached from [0.904171,
       0.704931]: travelled 11.73 m, remaining 0.26 m, plan 287 waypoints
=== slam: slam_toolbox maps a lap from the bridged /scan + /tf
[PASS] slam (82.5 s) - 472x473 @ 0.050 m, 420 occupied / 39741 free / 183095
       unknown; reloaded in a fresh process: 472x473, 405 occupied
=== reset: the car goes back to the origin (and stays there)
[PASS] reset (0.0 s) - back at (0.000, 0.000), nothing commanding the wheels

9/9 checks passed; report: bridge\docs\validation\acceptance-report.json
```

## 证据 1 · Windows 上像 ROS2 一样使用

守护进程先被停掉，再由 `ros2.bat` 自己完成「局域网扫描 → `Ping` 握手 → 建立持久 gRPC
通道 → 镜像全部远端话题」。

```bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2.bat topic list -t        :: 冷启动 1.3–2.8 s（等的是就绪文件，不是固定 sleep）
```

```
/clock [rosgraph_msgs/msg/Clock]
/cmd_vel [geometry_msgs/msg/Twist]
/map [nav_msgs/msg/OccupancyGrid]
/odom [nav_msgs/msg/Odometry]
/scan [sensor_msgs/msg/LaserScan]
/tf [tf2_msgs/msg/TFMessage]
/tf_static [tf2_msgs/msg/TFMessage]
```

> `topic list` 列出什么，取决于**当下谁在跑**，这一点跟 ROS2 一致。上面这次是干净状态
> （只有守护进程 + Ubuntu 仿真），所以是 7 个；实测到一次「整套 nav2 栈还活着」的时候，
> 同一个命令列出 **64 个** —— `planner_server` / `controller_server` / `bt_navigator` 这些节点的
> `/global_costmap/*`、`/controller_server/*` 都在里面。栈停掉之后再问，又回到 7 个：
> 读者侧按 pid 判断注册者是否还在，死掉的注册文件会被顺带清掉。

来源与频率：

```bat
bridge\windows\ros2.bat topic info /scan
::   Type: sensor_msgs/msg/LaserScan
::   Publisher count: 1   Subscription count: 0
::   Source: remote (bridged over gRPC)

bridge\windows\ros2.bat topic hz /scan       :: average 9.446 Hz over 55 samples
bridge\windows\ros2.bat bridge status        :: 7 topics (7 remote, 0 local)
```

Ubuntu 侧的雷达是 10 Hz；平均 9.45 Hz 的差值来自跨机传输和订阅端采样抖动。

## 证据 2 · Windows 发布，Ubuntu 订阅

```bat
bridge\windows\ros2.bat topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.25 angular.z=0.3 --rate 10 --duration 2
```

| 量 | 值 |
| --- | --- |
| `/odom` 位置（发之前） | (4.8713, -1.8646) |
| `/odom` 位置（发之后） | (4.5575, -1.3964) |
| 位移 | **0.564 m** |
| `/odom.twist.twist.linear.x` | **0.250**（与下发的速度一致） |

`/odom` 由 Ubuntu 的仿真小车产生并回传，所以位移和回显速度同时说明：命令经上行 gRPC
到达了小车，小车按该命令运动。

## 证据 3 · nav2 建图 + 规划 + 移动

```bat
python bridge\scripts\run_nav2_demo.py --goal auto --report run.json
```

| 项 | 值 |
| --- | --- |
| 代价地图 | 480 × 480（13728 lethal） |
| 规划 | NavFn，287 点 / 15.34 m |
| 行驶 | 23.0 s，**11.73 m** |
| 距目标 | **0.26 m** |

## 证据：转码逐字节一致

`bridge_cdr_check` 拿 `rosbags` 生成的 CDR 基准字节比自己转码的结果比：

```
cdr_to_shim(cdr) == shim        shim_to_cdr(shim) == cdr        cdr -> shim -> cdr 往返
```

**35 个 fixture、105 组字节比较、0 失败。**

## 证据：五棵测试树

| 树 | 命令 | 用例数 |
| --- | --- | --- |
| `nav2\ros2_shim` | `python nav2\ros2_shim\build.py` 然后 `python tools\run_ctest.py shim` | 32 |
| `nav2\shm_transport` | `python tools\run_ctest.py shm` | 1 |
| `bridge\windows` | `python bridge\scripts\build_bridge.py --test` | 6 |
| `gRPC\ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py --test` | 4 |
| `bridge\ubuntu\ros2_bridge_sim\tests` | `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q` | 71 |

* pytest 这棵**在 Windows 上跑**：Ubuntu 的 venv 里只有 grpc / numpy / protobuf，没有 pytest。
* `shim` 树里的用例各自带一个 `NAV2_SHM_NAMESPACE=ctest_<用例名>` —— `CMakeLists.txt` 末尾
  用 `ENVIRONMENT_MODIFICATION` 统一注入 —— 所以守护进程开着也不会和镜像进来的机器人
  抢同一条共享内存通道。守护进程停掉同样全绿。
* 只有并行才现形的问题用 `python tools\ctest_stress.py --rounds 50 --jobs 8` 压，
  结果落在 `.scratch/`（`ctest_stress.json` + 坏轮的 `stress_fail_NNN.log`）。

## 证据：跨平台一致

| 平台 | configure | 构建 | ctest |
| --- | --- | --- | --- |
| Windows / MSYS2 UCRT64 | 走 `ProtobufConfig.cmake`（`Protobuf_DIR` = MSYS2） | 0 error | **4/4** |
| Ubuntu 24.04 / g++ 13.3 | 走 CMake 自带的 `FindProtobuf` 模块（系统 protobuf 3.21.12） | 0 error | **4/4** |

`shm_transport` 在两个平台上都编过、跑过：

| 平台 | 结果 |
| --- | --- |
| Windows / MSYS2 UCRT64（MinGW） | 5 条全绿，连跑 3 次 |
| Ubuntu 24.04 / glibc 2.39 / g++ 13.3 | 7 条全绿，连跑 5 次（含一次在 6 核全量编译负载下） |
| Alpine 3.24 / musl（Ubuntu 上的 chroot） | 7 条全绿，连跑 5 次，吞吐 1.96 M msg/s |

## 证据：真机端到端（2026-10-30，全 PASS）

三个 ROS2 世界（Ubuntu 真 ROS2、Ubuntu 仿真、Windows 兼容层 nav2）在一条链路里同时存在。

| 检查 | 结果 |
| --- | --- |
| `probe_merged_graph.py` | 合并图 100+ 话题、438 service、21 action |
| `compare_agents.py` | `/scan` `/odom` `/clock` `/tf` 保住仿真计数；真 ROS2 的挂 `/real/*` |
| `check_daemon_vs_relay.py` | 守护进程镜像成功，`ros2 topic echo /chatter` 逐条读出 |
| `check_cmd_vel_to_sim.py` | 车从 0.0 → **0.600 m**，PASS |
| `check_merged_service.py` | `/sim/reset`（仿真）返回车位；`/add_two_ints`（真 ROS2）2+40=42，PASS |
| `check_merged_action.py` | `/sim/spin` accepted + SUCCEEDED（goal_id 带 `sim:`），PASS |
| pytest | **82 passed** |

## 证据：性能基线

共享内存的完整路径（真的 serialize / deserialize）：

| 载荷 | 线上大小 | 同进程 msg/s | 跨进程 msg/s | 延迟 p50 |
| --- | --- | --- | --- | --- |
| Twist | 48 B | 1.32 M | 1.40 M | 0.5 µs |
| Odometry | 709 B | 462 k | 483 k | 1.6 µs |
| LaserScan | 5813 B | 87 k | 88 k | 8.8 µs |

**跨进程 ≈ 同进程** —— 这是「共享内存本来就不为进程边界付钱」的最好证据。

`ros2 topic pub` 的定时精度（Windows 默认定时器粒度 15.6 ms，所以按 `sleep_for(1000/rate)`
毫秒睡会只剩 ~64 Hz）：

| 请求 | 实测 | 误差 |
| --- | --- | --- |
| 10 Hz | 10.0 Hz | 0.1% |
| 100 Hz | 100.4 Hz | 0.4% |
| 500 Hz | 500.0 Hz | 0.0% |
| 1000 Hz | 1002.7 Hz | 0.3% |

> **注意**：`rclcpp` 的 executor 轮询循环里还有 `sleep_for(1ms)`，所以**节点自己的定时器**
> 最高仍约 64 Hz。nav2 的 20–50 Hz 不受影响，但要跑 kHz 级节点定时器的话，这是下一个要改的地方。

## 变异验证

改测试断言时先确认它**会**失败：把断言改坏、看它红，再改回来。这条在
`bridge/docs/03-validation.md` 里叫「变异验证」，是这个仓库的常规要求。

---

# English

## Acceptance verdict

| # | Criterion | Verdict | Result |
| --- | --- | --- | --- |
| 1 | The gRPC SDK sweeps and establishes a persistent connection; after a bat loads the environment, `ros2 topic list` and friends behave as usual | After a cold start `ros2.bat topic list -t` returns 9 topics with all types correct; the duration follows how fast the daemon comes up (1.88 s most recently, 1.31 s in the field run when the machine was busy), but the result no longer varies with it | **PASS** |
| 2 | A topic published from Windows is subscribed normally on Ubuntu | Windows publishes `/cmd_vel`; the Ubuntu car moves **0.81 m**, `/odom.twist.linear.x` = 0.250 | **PASS** |
| 3 | nav2 subscribes, maps, plans and drives the car | 480×480 costmap (13728 lethal), NavFn plan of 287 points / 15.34 m, 23.0 s of driving for **11.73 m**, **0.26 m** from the goal | **PASS** |
| 4 | Port `nav2_amcl` + `slam_toolbox` (humble branch) to Windows: statically linked, no MSYS2 at run time | `objdump -p` on the ported binaries shows only `KERNEL32.dll` + `api-ms-win-crt-*`; with `PATH` reduced to `C:\Windows\System32;C:\Windows` and `ROS2_BRIDGE_MSYS2` pointing at a nonexistent directory, the daemon still starts and `ros2.bat topic list -t` plus upstream publishing work | **PASS** |
| 5 | The planner's **plugin layer**: `nav2_smac_planner` (Hybrid-A*) plugged in, and the whole stack still maps, plans and closes the loop with Smac instead of NavFn | Three consecutive `nav2_stack.bat --params <Smac>` runs all `SUCCEEDED`: **2.36 / 2.79 / 2.44 m** driven, `/plan` 2 / 17 / 6 messages, `/cmd_vel` 118 / 590 / 133; `planner_server`'s log shows `Created global planner plugin GridBased of type nav2_smac_planner::SmacPlannerHybrid` → `Using motion model: Dubin` → `Managed nodes are active` | **PASS** |
| 6 | Shared-memory geometry has a price: `map`/`costmap` topics are no longer reserved as 64 MB × 8 slots | `planner_server` resident memory **6191 MB → 231 MB**, `bt_navigator` 1277 MB → 319 MB, free memory 0.29 GB → 3.18 GB; before the change the Smac stack could not even start (a `std::bad_alloc` in `bt_navigator`), after it three runs in a row all pass | **PASS** |

## One-command reproduction

```bat
cd E:\DesktopData\app\code\CPU
python bridge\scripts\acceptance_check.py
```

The most recent run (`bridge/docs/validation/acceptance-report.json`):

```
=== topic_list: Windows lists the Ubuntu topics (cold start)
[PASS] topic_list (1.9 s) - 9 topics with types in 2.0 s (cold start)
=== topic_info: Windows resolves /scan to the gRPC peer
[PASS] topic_info (0.2 s) - reported as remote (bridged over gRPC)
=== scan_rate: Mirrored /scan keeps its 10 Hz
[PASS] scan_rate (6.2 s) - 9.64 Hz average over 55 samples
=== uplink: Windows /cmd_vel moves the Ubuntu robot
[PASS] uplink (4.7 s) - moved 0.81 m, /odom twist.linear.x=0.250 while commanded
=== uplink_nav2: the plan nav2 publishes reaches the robot (default uplink table)
[PASS] uplink_nav2 (11.6 s) - default table armed /plan, /behavior_tree_log;
       /plan forwarded 14/20 msgs, failed=0
=== cmd_vel_watchdog: a /cmd_vel that stops arriving stops the robot
[PASS] cmd_vel_watchdog (4.6 s) - drove 0.37 m on a 1 s command, then 0.000 m
       in the next second, /odom twist.linear.x=0
=== nav2_demo: nav2 plans a path and drives to the goal
[PASS] nav2_demo (22.8 s) - goal [-8.775, -6.375] reached from [0.904171,
       0.704931]: travelled 11.73 m, remaining 0.26 m, plan 287 waypoints
=== slam: slam_toolbox maps a lap from the bridged /scan + /tf
[PASS] slam (82.5 s) - 472x473 @ 0.050 m, 420 occupied / 39741 free / 183095
       unknown; reloaded in a fresh process: 472x473, 405 occupied
=== reset: the car goes back to the origin (and stays there)
[PASS] reset (0.0 s) - back at (0.000, 0.000), nothing commanding the wheels

9/9 checks passed; report: bridge\docs\validation\acceptance-report.json
```

## Evidence 1 — using ROS2 on Windows

The daemon is stopped first; then `ros2.bat` itself performs "LAN sweep → `Ping` handshake →
persistent gRPC channel → mirror every remote topic".

```bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2.bat topic list -t        :: cold start 1.3–2.8 s
```

```
/clock [rosgraph_msgs/msg/Clock]
/cmd_vel [geometry_msgs/msg/Twist]
/map [nav_msgs/msg/OccupancyGrid]
/odom [nav_msgs/msg/Odometry]
/scan [sensor_msgs/msg/LaserScan]
/tf [tf2_msgs/msg/TFMessage]
/tf_static [tf2_msgs/msg/TFMessage]
```

> What `topic list` shows depends on **who is running right now**, exactly as in ROS2. The run
> above was in a clean state (daemon + Ubuntu simulator), hence 7 topics; in one run with the
> whole nav2 stack alive the same command listed **64** — the `/global_costmap/*` and
> `/controller_server/*` of `planner_server` / `controller_server` / `bt_navigator` are in there.
> After the stack stops it is back to 7: the reader side judges by pid whether the registrar is
> still alive and cleans up the dead registry files.

Source and rate:

```bat
bridge\windows\ros2.bat topic info /scan
::   Type: sensor_msgs/msg/LaserScan
::   Publisher count: 1   Subscription count: 0
::   Source: remote (bridged over gRPC)

bridge\windows\ros2.bat topic hz /scan       :: average 9.446 Hz over 55 samples
bridge\windows\ros2.bat bridge status        :: 7 topics (7 remote, 0 local)
```

The Ubuntu lidar runs at 10 Hz; the 9.45 Hz average comes from the cross-machine transfer and
subscription-side sampling jitter.

## Evidence 2 — Windows publishes, Ubuntu subscribes

```bat
bridge\windows\ros2.bat topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.25 angular.z=0.3 --rate 10 --duration 2
```

| Quantity | Value |
| --- | --- |
| `/odom` position (before) | (4.8713, -1.8646) |
| `/odom` position (after) | (4.5575, -1.3964) |
| Displacement | **0.564 m** |
| `/odom.twist.twist.linear.x` | **0.250** (matching the commanded speed) |

`/odom` is produced by the Ubuntu simulator and echoed back, so the displacement and the echoed
velocity together show: the command reached the car over the upstream gRPC and the car moved
accordingly.

## Evidence 3 — nav2 maps, plans and drives

```bat
python bridge\scripts\run_nav2_demo.py --goal auto --report run.json
```

| Item | Value |
| --- | --- |
| Costmap | 480 × 480 (13728 lethal) |
| Plan | NavFn, 287 points / 15.34 m |
| Driving | 23.0 s, **11.73 m** |
| Distance to goal | **0.26 m** |

## Evidence — byte-exact transcoding

`bridge_cdr_check` compares its own transcoding against CDR reference bytes generated by
`rosbags`:

```
cdr_to_shim(cdr) == shim        shim_to_cdr(shim) == cdr        cdr -> shim -> cdr round trip
```

**35 fixtures, 105 byte comparisons, 0 failures.**

## Evidence — the five test trees

| Tree | Command | Cases |
| --- | --- | --- |
| `nav2\ros2_shim` | `python nav2\ros2_shim\build.py` then `python tools\run_ctest.py shim` | 32 |
| `nav2\shm_transport` | `python tools\run_ctest.py shm` | 1 |
| `bridge\windows` | `python bridge\scripts\build_bridge.py --test` | 6 |
| `gRPC\ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py --test` | 4 |
| `bridge\ubuntu\ros2_bridge_sim\tests` | `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q` | 71 |

* The pytest tree **runs on Windows**: the Ubuntu venv only has grpc / numpy / protobuf.
* Every case in the `shim` tree carries `NAV2_SHM_NAMESPACE=ctest_<case name>`, injected uniformly
  by `ENVIRONMENT_MODIFICATION` at the end of `CMakeLists.txt`, so a running daemon does not fight
  the mirrored robot for the same shared-memory channel. Equally green with the daemon stopped.
* Problems that only show up under parallelism are pressed with
  `python tools\ctest_stress.py --rounds 50 --jobs 8`; the results land in `.scratch/`
  (`ctest_stress.json` plus `stress_fail_NNN.log` for the bad rounds).

## Evidence — cross-platform parity

| Platform | configure | build | ctest |
| --- | --- | --- | --- |
| Windows / MSYS2 UCRT64 | via `ProtobufConfig.cmake` (`Protobuf_DIR` = MSYS2) | 0 errors | **4/4** |
| Ubuntu 24.04 / g++ 13.3 | via CMake's own `FindProtobuf` module (system protobuf 3.21.12) | 0 errors | **4/4** |

`shm_transport` was configured, built and run on both:

| Platform | Result |
| --- | --- |
| Windows / MSYS2 UCRT64 (MinGW) | 5/5 green, 3 runs in a row |
| Ubuntu 24.04 / glibc 2.39 / g++ 13.3 | 7/7 green, 5 runs in a row (once under a full 6-core compile load) |
| Alpine 3.24 / musl (a chroot on Ubuntu) | 7/7 green, 5 runs in a row, throughput 1.96 M msg/s |

## Evidence — real-machine end to end (2026-10-30, all PASS)

Three ROS2 worlds (real ROS2 on Ubuntu, the simulator on Ubuntu, the compatibility layer's nav2 on
Windows) coexisting in one chain.

| Check | Result |
| --- | --- |
| `probe_merged_graph.py` | Merged graph: 100+ topics, 438 services, 21 actions |
| `compare_agents.py` | `/scan` `/odom` `/clock` `/tf` keep the simulation's counts; the real ROS2 ones move to `/real/*` |
| `check_daemon_vs_relay.py` | The daemon mirrors successfully; `ros2 topic echo /chatter` reads every message |
| `check_cmd_vel_to_sim.py` | The car goes from 0.0 → **0.600 m**, PASS |
| `check_merged_service.py` | `/sim/reset` (simulation) returns the car pose; `/add_two_ints` (real ROS2) gives 2+40=42, PASS |
| `check_merged_action.py` | `/sim/spin` accepted + SUCCEEDED (goal_id carries `sim:`), PASS |
| pytest | **82 passed** |

## Evidence — performance baseline

The full shared-memory path (a real serialize / deserialize):

| Payload | Wire size | Same-process msg/s | Cross-process msg/s | Latency p50 |
| --- | --- | --- | --- | --- |
| Twist | 48 B | 1.32 M | 1.40 M | 0.5 µs |
| Odometry | 709 B | 462 k | 483 k | 1.6 µs |
| LaserScan | 5813 B | 87 k | 88 k | 8.8 µs |

**Cross-process ≈ same-process** — the best evidence that shared memory was never paying for the
process boundary.

Timing precision of `ros2 topic pub` (Windows' default timer granularity is 15.6 ms, so sleeping
`1000/rate` milliseconds used to yield only ~64 Hz):

| Requested | Measured | Error |
| --- | --- | --- |
| 10 Hz | 10.0 Hz | 0.1% |
| 100 Hz | 100.4 Hz | 0.4% |
| 500 Hz | 500.0 Hz | 0.0% |
| 1000 Hz | 1002.7 Hz | 0.3% |

> **Note**: the `rclcpp` executor poll loop still contains a `sleep_for(1ms)`, so **a node's own
> timers** still top out around 64 Hz. nav2's 20–50 Hz is unaffected, but kHz-class node timers are
> the next thing to change.

## Mutation testing

When changing an assertion, first confirm it **will** fail: break it, watch it go red, put it back.
This is called "mutation verification" in `bridge/docs/03-validation.md` and is this repository's
standing practice.
