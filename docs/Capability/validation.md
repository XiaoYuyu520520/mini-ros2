---
layout: default
title: 5 能力清单 · 验收与实测数据
---

# 5.2 验收与实测数据

<div class="overview">
<div class="label">本页总览</div>
<p>本节列出验收标准、一键复现方法、以及各条实测证据。</p>
<ul>
  <li><a href="#verdict">验收结论</a></li>
  <li><a href="#reproduce">一键复现</a></li>
  <li><a href="#evidence">实测证据</a></li>
  <li><a href="#tests">五棵测试树</a></li>
</ul>
</div>

## 5.2.1 验收结论

| # | 验收标注 | 判定 | 结论 |
| --- | --- | --- | --- |
| 1 | gRPC SDK 快速扫描建立持久连接；一个 bat 临时加载环境后即可用 ros2，`ros2 topic list` 等保持一致 | 冷启动后 `ros2.bat topic list -t` 返回 9 个话题且类型全部正确；耗时随守护进程起多快而变（最近一次冷启动 1.88 s，现场那次 1.31 s），但结果不再随之变化 | **通过** |
| 2 | Windows 发布话题，Ubuntu 端也能正常订阅 | Windows 发 `/cmd_vel`，Ubuntu 小车位移 **0.81 m**，`/odom.twist.linear.x` = 0.250 | **通过** |
| 3 | nav2 正常订阅话题建图、规划路径、指挥小车移动 | 480×480 costmap（13728 lethal）、NavFn 规划 287 点 / 15.34 m、23.0 s 行驶 **11.73 m**，距目标 **0.26 m** | **通过** |
| 4 | 移植 `nav2_amcl` + `slam_toolbox` 到 Windows：静态编译、运行时不依赖 MSYS | `objdump -p` 里只剩 `KERNEL32.dll` + `api-ms-win-crt-*`；`PATH` 只留 `C:\Windows\System32;C:\Windows`、`ROS2_BRIDGE_MSYS2` 指向不存在的目录时，守护进程照常起 | **通过** |
| 5 | 规划器插件层：`nav2_smac_planner`（Hybrid-A*）接入，规划器从 NavFn 换成 Smac 后整栈照常建图、规划、闭环 | 连跑三次全部 `SUCCEEDED`：行驶 **2.36 / 2.79 / 2.44 m**，`/plan` 2 / 17 / 6 条，`/cmd_vel` 118 / 590 / 133 条 | **通过** |
| 6 | 共享内存通道的几何是有代价的：`map`/`costmap` 类话题不再按 64 MB × 8 槽预留 | `planner_server` 常驻 **6191 MB → 231 MB**、`bt_navigator` 1277 MB → 319 MB，空闲内存 0.29 GB → 3.18 GB | **通过** |

## 5.2.2 一键复现

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

## 5.2.3 实测证据

### 证据 1 · Windows 上像 ROS2 一样使用

守护进程先被停掉，再由 `ros2.bat` 自己完成「局域网扫描 → `Ping` 握手 → 建立持久 gRPC
通道 → 镜像全部远端话题」。

```bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2.bat topic list -t        :: 冷启动 1.3–2.8 s
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

```bat
bridge\windows\ros2.bat topic info /scan
::   Type: sensor_msgs/msg/LaserScan
::   Publisher count: 1   Subscription count: 0
::   Source: remote (bridged over gRPC)

bridge\windows\ros2.bat topic hz /scan       :: average 9.446 Hz over 55 samples
bridge\windows\ros2.bat bridge status        :: 7 topics (7 remote, 0 local)
```

Ubuntu 侧的雷达是 10 Hz；平均 9.45 Hz 的差值来自跨机传输和订阅端采样抖动。

### 证据 2 · Windows 发布，Ubuntu 订阅

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

### 证据 3 · nav2 建图 + 规划 + 移动

```bat
python bridge\scripts\run_nav2_demo.py --goal auto --report run.json
```

| 项 | 值 |
| --- | --- |
| 代价地图 | 480 × 480（13728 lethal） |
| 规划 | NavFn，287 点 / 15.34 m |
| 行驶 | 23.0 s，**11.73 m** |
| 距目标 | **0.26 m** |

### 证据 4 · 真机端到端（2026-10-30，全 PASS）

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

### 证据 5 · 性能基线

| 载荷 | 线上大小 | 同进程 msg/s | 跨进程 msg/s | 延迟 p50 |
| --- | --- | --- | --- | --- |
| Twist | 48 B | 1.32 M | 1.40 M | 0.5 µs |
| Odometry | 709 B | 462 k | 483 k | 1.6 µs |
| LaserScan | 5813 B | 87 k | 88 k | 8.8 µs |

`ros2 topic pub` 的定时精度（Windows 默认定时器粒度 15.6 ms，所以按 `sleep_for(1000/rate)`
毫秒睡会只剩 ~64 Hz）：

| 请求 | 实测 | 误差 |
| --- | --- | --- |
| 10 Hz | 10.0 Hz | 0.1% |
| 100 Hz | 100.4 Hz | 0.4% |
| 500 Hz | 500.0 Hz | 0.0% |
| 1000 Hz | 1002.7 Hz | 0.3% |

<div class="callout note">
<span class="label">注意</span>
<p><code>rclcpp</code> 的 executor 轮询循环里还有 <code>sleep_for(1ms)</code>，所以
<strong>节点自己的定时器</strong>最高仍约 64 Hz。nav2 的 20–50 Hz 不受影响，但要跑 kHz 级节点
定时器的话，这是下一个要改的地方。</p>
</div>

## 5.2.4 五棵测试树

| 树 | 命令 | 用例数 |
| --- | --- | --- |
| `nav2\ros2_shim` | `python nav2\ros2_shim\build.py` 然后 `python tools\run_ctest.py shim` | 32 |
| `nav2\shm_transport` | `python tools\run_ctest.py shm` | 1 |
| `bridge\windows` | `python bridge\scripts\build_bridge.py --test` | 6 |
| `gRPC\ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py --test` | 4 |
| `bridge\ubuntu\ros2_bridge_sim\tests` | `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q` | 71 |

* pytest 这棵**在 Windows 上跑**：Ubuntu 的 venv 里只有 grpc / numpy / protobuf，没有 pytest。
* `shim` 树里的用例各自带一个 `NAV2_SHM_NAMESPACE=ctest_<用例名>`，所以守护进程开着也不会和
  镜像进来的机器人抢同一条共享内存通道。

## 5.2.5 跨平台一致

| 平台 | configure | 构建 | ctest |
| --- | --- | --- | --- |
| Windows / MSYS2 UCRT64 | 走 `ProtobufConfig.cmake`（`Protobuf_DIR` = MSYS2） | 0 error | **4/4** |
| Ubuntu 24.04 / g++ 13.3 | 走 CMake 自带的 `FindProtobuf` 模块（系统 protobuf 3.21.12） | 0 error | **4/4** |

## 5.2.6 变异验证

改测试断言时先确认它**会**失败：把断言改坏、看它红，再改回来。
这叫「变异验证」，是这个仓库的常规要求。

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="overview">5.1 已实现能力</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="../FAQ/limitations">6.1 功能限制与缺口</a></div>
</div>