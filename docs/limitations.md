---
layout: default
title: 限制与缺口 Limitations
---

# 限制与缺口 Limitations

> **[中文](#中文) · [English](#english)**

---

# 中文

这一页只讲**缺口**。已经能跑的东西在 [能力清单](capabilities.html)里有证据，这里不重复。

每条写三件事：**现在是什么样**、**缺什么**、**缺了会有什么后果**。

## 一句话结论

三条验收（Windows 当 ROS2 用 / Windows 发布、Ubuntu 订阅 / nav2 建图规划指挥车）**已经成立**。
还缺的东西按「现在挡着什么」排：

1. **跨机只有一张编解码表**：40 种话题类型、38 种 service、20 个 action；表外的类型过不去，
   带数组字段的 goal（`SmoothPath` / `FollowWaypoints` / `NavigateThroughPoses`）没有命令行入口。
2. **对端从来不是真 ROS2**（早期状态）：Ubuntu 那台机器上没装 ROS2，`ros2_bridge_sim` 是按
   proto 自己写的 Python 对端；SDK 的 `RclcppBackend` 从没编译过。
   —— **这一条后来变了**：机器人上现在装的是真的 ROS2 Jazzy，
   `ROS2_GRPC_SDK_WITH_ROS2=ON` 的 `RclcppBackend` 编出来并在跑，真图上的话题、服务、动作
   列表都端到端量过。仿真那一行仍然成立：`ros2_bridge_sim` 仍是默认的验收底座，
   它仍然没有动力学与传感器噪声模型。
3. **nav2 上游还差 7 个包**（`nav2_theta_star_planner`、`nav2_constrained_smoother`、
   `nav2_following`、`nav2_loopback_sim`、`nav2_system_tests`、`nav2_rviz_plugins`、
   `nav2_simple_commander`）；SLAM 侧只差 `merge_maps_kinematic.exe` 的**合并语义**这一件。
4. **兼容层没有 rclpy、rosbag2、launch / launch_ros**。
5. **工程化那一层（版本控制 / CI / 安装包）** 目前基本为零。它不挡功能，挡的是「别人能不能复现」。

---

## A. nav2：上游 46 个包，还缺 7 个

当前构建覆盖 25 个 `nav2_port_package(...)` 加上直接编译的 `nav2_amcl` /
`nav2_behavior_tree` / `nav2_util_core` / `nav2_voxel_grid` / `nav2_ros_common`，
加上只出消息的 `nav2_msgs` / `dwb_msgs` / `nav_2d_msgs`。

| 还缺的包 | 现在是什么样 | 缺了会有什么后果 |
| --- | --- | --- |
| `nav2_theta_star_planner` | 没有移植，`build` 目录里 0 个产物 | 全局规划器少一种（上游可选插件之一）；对当前链路无影响 |
| `nav2_constrained_smoother` | 没有移植 | 平滑器少一种（要平滑度约束时用不了）；`smoother_server` 现在的 `SmoothPath` 仍然可用 |
| `nav2_following`（`opennav_following`） | 没有移植 | 「跟人 / 跟车」整个功能不可用（对本项目目标优先级最低） |
| `nav2_loopback_sim` | 没有移植 | 没有「上游自带的仿真机器人」这个替代品；本项目的仿真在 Ubuntu 侧 |
| `nav2_system_tests` | 没有移植 | 上游那套系统集成测试跑不了；本项目的回归是自己那几套 |
| `nav2_rviz_plugins` | 没有移植，Windows 上也没有 RViz | 看不到建图/规划过程；现在只能靠 `map_saver_cli.exe` 存 PGM / PNG 再看 |
| `nav2_simple_commander` | 没有移植（纯 Python，要 rclpy） | 没有 Python API；发目标只能用 C++（`nav2_goal.exe` / `nav2_waypoints.exe`）或 CLI |

**不需要单独移植的**：`nav2_core` 与 `opennav_docking_core` 只有头文件；`nav2_util` 的源码被收进
`nav2_util_core`；`nav2_msgs` / `dwb_msgs` / `nav_2d_msgs` 由生成器出消息；
`nav2_bringup` / `nav2_common` 是配置包（真正被用的是 `params/nav2_params.yaml`；
上游的 `launch/` 那一套语法本兼容层没有）。

### docking / route 只到「装得起来」

`docking_server` 与 `route_server` 已经被拉到 ACTIVE，但两个都没有数据：仓库里没有 dock 实例
配置，也没有 `.geojson` 路网文件。`ComputeRoute` 会答 `No graph set! Aborting request.`，
跟随 / 回充的端到端动作一次都没走过。

---

## B. 兼容层（`ros2_shim`）：ROS2 的表面还缺哪些

| 缺口 | 现状 | 缺了会有什么后果 |
| --- | --- | --- |
| **rclpy** | 没有 Python 绑定。`python/pymessages` 与 `py_interop` 是 ctypes + 生成的消息类，**不是 rclpy** | 依赖 rclpy 的东西全都用不了：`nav2_simple_commander`、上游 `nav2_system_tests`、任何「写个脚本节点」的用法 |
| **rosbag2** | 完全没有 | 不能录、不能放；复现一次跑只能重跑一遍。跨机时也没有「在 Windows 录 Ubuntu 的话题」这条路 |
| **launch / launch_ros** | 没有。启动靠 `bridge\windows\*.bat` 与 `bridge/scripts/run_*.py` | 上游的 `launch` 文件用不了（连带 remap、参数覆写、条件启动那一套语法）；换一个组合就要写 / 改一个 bat |
| **image_transport** | 只有 `point_cloud_transport` 的桩；没有 `image_transport` | `sensor_msgs/Image` 这类相机话题没有传输层，也没有压缩插件 |
| **RViz / rqt** | 没有，也不在计划里 | 「看得见」这件事只能用离线图或 CLI 顶替 |
| **tf 工具** | `tf2` / `tf2_ros` 库在，`tf_probe` / `tf_listener_probe` 是自带的诊断工具 | 没有 `static_transform_publisher`、`tf2_echo` / `tf2_monitor` 这些命令行工具 |
| **QoS** | 只有声明（可靠信道 / 历史深度落在本地环形缓冲）。**枚举的拼法有差异**：shim 写的是 `RELIABLE` / `BEST_EFFORT` / `KEEP_LAST`，上游写的是 `Reliable` / `BestEffort` / `KeepLast`。`nav2_extracted` 里**一个都没直接写枚举名**（nav2 走的是 `.reliable()` / `SystemDefaultsQoS()` / 参数字符串），所以现状编得过 | 没有 DDS 那套「同一话题不同 QoS 不匹配」的行为 —— 这是设计选择，不是缺陷。真正的遗留是**源码级拼法差异**：哪天有新代码按上游写 `rclcpp::ReliabilityPolicy::Reliable`，这里会编不过；补一组别名是几行的事 |
| **组件 / 组合** | `rclcpp_components` 只有 `register_node_macro.hpp`，没有 component container 的 `load_component` 运行路径 | 上游那种「一个进程装多个组件」的部署方式用不了；这里每个服务是一个 exe |
| **仿真时钟** | `use_sim_time` 只在那几个节点上打开；`rclcpp::utilities::set_use_sim_time()` 仍是空实现 | 没打开 `use_sim_time` 的节点里，「拿 `now()` 配消息戳」的代码会向未来外推 |
| **CLI 面** | 有 `topic`（list / info / type / echo / hz / pub / find）、`service`（list / type / find / call）、`action`（list / type / find / send_goal / cancel）、`param`（list / get / set / describe / dump）、`node list`、`interface list`、`bridge status` | 没有 `ros2 bag`、`ros2 run`、`ros2 launch`、`ros2 lifecycle`、`node info`、`interface show`、`topic bw` / `topic delay`。另外 `service call` 只认编解码表里的类型 |

**已经补上的（别再当缺口）**：参数服务、`/parameter_events`、话题 remap（`-r FROM:=TO`）、
`/rosout` 日志话题、8 位 PNG coder、位姿图存档、`-p` 的列表值。

---

## C. 跨机（`bridge` + `ros2_gRPC_sdk`）：边界是编解码表

三张表都在 `bridge\windows\src\` 里：`type_registry.cpp`（话题）、`service_registry.cpp`、
`action_registry.cpp`。

| 面 | 现在覆盖 | 表外会怎样 |
| --- | --- | --- |
| 话题 | **40 种** | 类型不在表里就**过不去**：`sensor_msgs/JointState` / `CompressedImage`、`std_msgs/Float64`…（本机 shm 上没这个限制：29 个消息包都生成了） |
| service | **38 种** | 表外还有 **45 种**跨不了机：`rcl_interfaces` 的 8 个参数服务、`robot_localization` 的 8 个、`map_msgs` 的 6 个、`dwb_msgs` 的 5 个、`geographic_msgs` 的 4 个…；本机 shm 上没这个限制 |
| action | **20 个** | **带数组字段的 goal**（`poses` / `viapoints` / `path`）`build_goal` 会明确拒绝，`ros2 action send_goal` 发不出去（本机 shm 上 `SmoothPath` / `FollowWaypoints` 已经端到端跑通） |

其它跨机缺口：

- **proto 里没有的 RPC**：一共 14 个 RPC，全是话题 / 服务 / action。**没有参数读写**
  （参数服务是本机的）、**没有 TF 查询**、**没有 bag**。所以「Ubuntu 上直接改 Windows 节点的
  参数」这件事**连协议都没有**。
- **一次只对一个 agent**：`bridge_daemon.cpp` 的 `connectToAgent()` 是单数，discovery 虽然能列出
  最多 32 个，但只连最近的那一个。多机器人场景没做。（中继的存在就是为了把两张图合并成一张，
  因为守护进程只能连一个。）
- **安全**：客户端有 `use_tls` / `tls_root_cert_pem` / `tls_target_name_override` 三个开关，
  但默认明文、**从没验过**；没有 mTLS / 鉴权 / 压缩。
- **版本不比对**：`Ping` 会带回对端的 SDK 版本，但客户端拿到之后**没有任何检查**。
  两端版本不一致时不会有提示，只会以「某个 RPC 行为不对」的形式表现出来。
- **订阅没有队列，也没有丢弃统计**：客户端的订阅循环是同步的，本地消费慢的时候只有 HTTP/2 自己
  的流控兜底；没有背压策略、没有积压 / 丢弃计数。也没有开 gRPC 压缩。
- **网络假设**：Ubuntu 要在扫描可达的同一网段内；跨网段要 `--agent host:port`（没有中继 / 网关）。

---

## D. 对端真实性

**这条最容易被忽略。**

早期状态：Ubuntu 上**没有装 ROS2**，对端是按 proto 手写的 Python 仿真（差分小车 + 合成雷达，
7 个话题）；SDK 的 `RclcppBackend` **从没编译过、也没跑过**。后果是：那套东西证明的是
**协议与兼容层是对的**，但「接到真 ROS2 图上会怎样」没有任何证据。

**现在的状态**：机器人上装的是**真的 ROS2 Jazzy**（`bridge/scripts/ubuntu_ros2_setup.sh`），
`ROS2_GRPC_SDK_WITH_ROS2=ON` 的 `RclcppBackend` 编出来并在跑
（`bridge/scripts/ubuntu_ros2_peer.sh`，端口 50111），真图上的话题、服务、动作列表都对着它量过。
顺带撞出并修掉了**两处只在真对端出现的 bug** —— 空 CDR 体被 Fast CDR 拒读、
`rmw_serialize` 拿到零 allocator。

**还差的**：量的是 demo_nodes / `std_srvs` / `action_tutorials` 那几条，
**真 nav2 栈上的服务、话题与动作还没对着一台真 ROS2 量过**。

**仿真仍然成立的两条**：仿真没有动力学与传感器噪声模型；`rosbag2` 仍然缺。

---

## E. slam_toolbox：缺的是「周边」而不是「核心」

建图主链路是完整的（sync / async 节点都能起，Karto 的扫描匹配、位姿图、回环检测、Eigen
求解器都在，归档做到了逐母子），而且「建图 → 存图 → 把存下来的图喂回 `map_server`」这一整圈
现在有活的证据。

| 缺口 | 现状 | 代价 |
| --- | --- | --- |
| **`merge_maps_kinematic.exe`** | 编出来了；**两个服务真调过**（`add_submap` / `merge_submaps`），两个 codec 也补进了桥的表。剩下的是**语义**：后者要靠 interactive marker 的反馈来摆放子图，而这套 shim 里没有 rviz 那样的反馈源，零子图时它只是一趟空跑 | 多张子图**合并**这条路没验过。类型通路已经不是障碍了 |
| **`map_saver` 的 `system()` 路径** | `slam_toolbox` 自带的 `map_saver.cpp` 靠 `system("ros2 run nav2_map_server map_saver_cli ...")`，Windows 上必失败 | 用 `nav2\ros2_shim\build\map_saver_cli.exe`，没有其他代价 |
| **窄道 / 回环 / 长时间漂移 / 大图序列化** | 没测 | 地图质量在这些场景下的表现未知 |

---

## F. 工程化

| 缺口 | 现状 | 后果 |
| --- | --- | --- |
| **CI** | `.github/workflows/cross-platform.yml` 与 `release.yml` 已就位（Windows + Linux x86_64 + ARM64 三平台） | —— |
| **安装包 / 分发** | `package.py` 按平台出 zip / tar.gz，CI 在 tag 上发 Release 并附 `SHA256SUMS.txt` | Windows 侧运行要带 88 个 DLL；分发是「复制目录」，没有安装器 / 升级路径 |
| **性能基线** | 只有共享内存那一条吞吐（100 万条 / 608 ms） | 端到端时延（Ubuntu 雷达 → Windows 规划 → 回传）没有量化基线 |
| **故障注入 / 稳定性** | 测试里**没有一条**碰断线重连；对端重启、LAN 抖动、丢包都没试过；也没有长时间 soak 跑 | 守护进程里那段「流不是永生的、要重订阅」的逻辑**没有被验证过**；长时间跑会不会漏内存、会不会把槽位耗光，也没有数据 |

---

## G. 有二进制、但没有证据的清单

这些东西**编译通过了、有的还注册进了插件表**，但一次都没真跑过（或只跑到「创建 + 激活」）：

| 产物 | 状态 |
| --- | --- |
| `assisted_teleop` / `drive_on_heading` | `behavior_server` 里创建并激活过，没有调用方 |
| `merge_maps_kinematic.exe` | 见 E 节：服务通路已验，还剩下「合并语义」没验 |
| `route_server` / `docking_server` | 没有图文件 / 没有 dock 实例，只到 ACTIVE |
| MPPI 的 `AckermannMotionModel` / `OmniMotionModel` / 3 个未启用 critic / `visualize` | 用的是上游默认那一套（8 个 critic），一次都没调过 |
| SDK 的 TLS 通道 | 开关在，从没走过加密连接 |
| `/parameter_events` 跨机 | 类型**已经进了编解码表**，`ros2 topic echo` 打得出 `node:` 与参数变化；**跨机端到端还没实测**（proto 里仍然没有对应的 RPC，只有话题转发这条路） |
| `bt_smoke.exe` / `tf_probe.exe` / `tf_listener_probe.exe` / `scan_path_probe.exe` / `amcl_probe.exe` / `parameter_events_probe.exe` | 自带的诊断工具，不是回归的一部分 |

---

## H. 如果只能再做几件事

按「每单位功夫能取回多少能力」排：

1. **跨机数组 codec 的命令行入口**（`SmoothPath` / `FollowWaypoints` 的 `poses` / `viapoints`）。
   本机已经跑通，跨机只差 `build_goal` 里数组字段的解析。
2. **`nav2_theta_star_planner` / `nav2_constrained_smoother`**（各一个包，形状与已移植的
   规划器 / 平滑器相同）。
3. **真 nav2 栈对着一台真 ROS2 量一遍**（话题、服务、动作全套）。
4. **跨机参数读写**（proto 加 RPC）。
5. **版本控制 + CI 的第一次真跑**。
6. **`merge_submaps` 的合并语义**。

**不建议**现在去碰的：RViz 移植（整个 GUI 栈，收益只是「看得见」）、rclpy 全量绑定
（要重做一遍 Python 生态）、Gazebo / 物理仿真（本项目的仿真已经够验证链路）、
`nav2_system_tests` 移植（依赖 rclpy + 仿真器，性价比最低）。

---

# English

This page is only about the **gaps**. What already works is documented with evidence on
[Capabilities](capabilities.html).

Each item states three things: **what it is now**, **what is missing**, and **what it costs**.

## In one line

The three acceptance criteria (use ROS2 on Windows / publish from Windows and subscribe on Ubuntu /
nav2 maps, plans and drives the car) **hold**. What is left, ordered by what it blocks:

1. **Cross-machine support is bounded by one codec table**: 40 topic types, 38 service types,
   20 actions. Types outside the table cannot cross; goals with array fields
   (`SmoothPath` / `FollowWaypoints` / `NavigateThroughPoses`) have no command-line entry.
2. **The peer was never real ROS2** (the earlier state): the Ubuntu machine had no ROS2, and
   `ros2_bridge_sim` is a hand-written Python peer against the proto; the SDK's `RclcppBackend`
   had never been compiled.
   — **This changed later**: the robot now runs **real ROS2 Jazzy**, the `RclcppBackend` built with
   `ROS2_GRPC_SDK_WITH_ROS2=ON` is running, and the topic / service / action lists of a real graph
   were measured end to end. The simulation line still stands: `ros2_bridge_sim` is still the
   default acceptance base, and it still has no dynamics or sensor-noise model.
3. **nav2 is still missing 7 upstream packages** (`nav2_theta_star_planner`,
   `nav2_constrained_smoother`, `nav2_following`, `nav2_loopback_sim`, `nav2_system_tests`,
   `nav2_rviz_plugins`, `nav2_simple_commander`); on the SLAM side only the **merge semantics** of
   `merge_maps_kinematic.exe` remain.
4. **The compatibility layer has no rclpy, rosbag2, launch / launch_ros.**
5. **The engineering layer (version control / CI / packaging)** is close to zero. It does not block
   functionality; it blocks "can somebody else reproduce this".

---

## A. nav2 — 7 of the 46 upstream packages are missing

Coverage today: 25 `nav2_port_package(...)` calls plus the directly compiled `nav2_amcl` /
`nav2_behavior_tree` / `nav2_util_core` / `nav2_voxel_grid` / `nav2_ros_common`, plus the
message-only `nav2_msgs` / `dwb_msgs` / `nav_2d_msgs`.

| Missing package | Now | Consequence |
| --- | --- | --- |
| `nav2_theta_star_planner` | Not ported; 0 artefacts in `build` | One fewer global planner (an optional upstream plugin); no effect on the current chain |
| `nav2_constrained_smoother` | Not ported | One fewer smoother (unavailable when smoothness constraints are needed); `smoother_server`'s `SmoothPath` still works |
| `nav2_following` (`opennav_following`) | Not ported | The whole "follow a person / car" feature is unavailable (lowest priority here) |
| `nav2_loopback_sim` | Not ported | No substitute for upstream's own simulated robot; this project's simulator lives on the Ubuntu side |
| `nav2_system_tests` | Not ported | Upstream's system integration tests cannot run; this project's regression is its own suites |
| `nav2_rviz_plugins` | Not ported, and there is no RViz on Windows | You cannot watch mapping/planning; use `map_saver_cli.exe` to write PGM / PNG and look at that |
| `nav2_simple_commander` | Not ported (pure Python, needs rclpy) | No Python API; goals can only be sent from C++ (`nav2_goal.exe` / `nav2_waypoints.exe`) or the CLI |

**No separate port needed**: `nav2_core` and `opennav_docking_core` are header-only; `nav2_util`'s
sources are folded into `nav2_util_core`; `nav2_msgs` / `dwb_msgs` / `nav_2d_msgs` come from the
generator; `nav2_bringup` / `nav2_common` are configuration packages (what is actually used is
`params/nav2_params.yaml`; upstream's `launch/` syntax is not supported by this shim).

### Docking / route only reach "it loads"

`docking_server` and `route_server` are pushed to ACTIVE, but neither has data: there is no dock
instance configuration in the repo and no `.geojson` graph file. `ComputeRoute` answers
`No graph set! Aborting request.`, and the follow / dock end-to-end action has never run.

---

## B. The compatibility layer — what ROS2's surface is missing

| Gap | Now | Consequence |
| --- | --- | --- |
| **rclpy** | No Python bindings. `python/pymessages` and `py_interop` are ctypes plus generated message classes — **not rclpy** | Everything depending on rclpy is unavailable: `nav2_simple_commander`, upstream `nav2_system_tests`, any "write a script node" usage |
| **rosbag2** | None at all | Cannot record or replay; reproducing a run means running it again. There is also no "record Ubuntu's topics on Windows" |
| **launch / launch_ros** | None. Startup goes through `bridge\windows\*.bat` and `bridge/scripts/run_*.py` | Upstream `launch` files are unusable (and with them the remap / parameter-override / conditional-start syntax); every new combination means writing or editing a bat |
| **image_transport** | Only a `point_cloud_transport` stub; no `image_transport` | Camera topics such as `sensor_msgs/Image` have no transport layer and no compression plugins |
| **RViz / rqt** | None, and not planned | "Seeing it" has to be replaced by offline images or the CLI |
| **tf tools** | The `tf2` / `tf2_ros` libraries are there; `tf_probe` / `tf_listener_probe` are bundled diagnostics | No `static_transform_publisher`, no `tf2_echo` / `tf2_monitor` command-line tools |
| **QoS** | Declarations only (reliability and history depth land on the local ring buffer). **The enum spelling differs**: the shim writes `RELIABLE` / `BEST_EFFORT` / `KEEP_LAST`, upstream writes `Reliable` / `BestEffort` / `KeepLast`. `nav2_extracted` **never writes an enum name directly** (nav2 goes through `.reliable()` / `SystemDefaultsQoS()` / parameter strings), so it compiles today | No DDS-style "mismatched QoS on one topic" behaviour — a design choice, not a defect. The real leftover is the **source-level spelling difference**: new code written upstream-style as `rclcpp::ReliabilityPolicy::Reliable` will not compile here; adding aliases is a few lines |
| **Components / composition** | `rclcpp_components` has only `register_node_macro.hpp`, no component-container `load_component` path | Upstream-style "many components in one process" deployment is unavailable; here each server is its own exe |
| **Simulation clock** | `use_sim_time` is on only for those nodes; `rclcpp::utilities::set_use_sim_time()` is still an empty stub | In nodes without `use_sim_time`, code that pairs `now()` with a message stamp extrapolates into the future |
| **CLI surface** | `topic` (list / info / type / echo / hz / pub / find), `service` (list / type / find / call), `action` (list / type / find / send_goal / cancel), `param` (list / get / set / describe / dump), `node list`, `interface list`, `bridge status` | No `ros2 bag`, `ros2 run`, `ros2 launch`, `ros2 lifecycle`, `node info`, `interface show`, `topic bw` / `topic delay`. Also, `service call` only knows types in the codec table |

**Already filled in (do not treat these as gaps)**: parameter services, `/parameter_events`, topic
remap (`-r FROM:=TO`), the `/rosout` log topic, the 8-bit PNG coder, pose-graph archiving, and
list values for `-p`.

---

## C. Cross-machine — the boundary is the codec table

All three tables live in `bridge\windows\src\`: `type_registry.cpp` (topics),
`service_registry.cpp`, `action_registry.cpp`.

| Surface | Coverage | Outside the table |
| --- | --- | --- |
| Topics | **40 types** | The type **cannot cross**: `sensor_msgs/JointState` / `CompressedImage`, `std_msgs/Float64`… (the local shm has no such limit — 29 message packages are generated) |
| Services | **38 types** | **45 more** cannot cross: the 8 `rcl_interfaces` parameter services, 8 `robot_localization`, 6 `map_msgs`, 5 `dwb_msgs`, 4 `geographic_msgs`… (the local shm has no such limit) |
| Actions | **20** | Goals **with array fields** (`poses` / `viapoints` / `path`) are explicitly rejected by `build_goal`, so `ros2 action send_goal` cannot send them (on the local shm `SmoothPath` / `FollowWaypoints` already run end to end) |

Other cross-machine gaps:

- **RPCs the proto does not have**: 14 RPCs, all topics / services / actions. **No parameter
  read/write** (the parameter service is local), **no TF query**, **no bag**. So "change a Windows
  node's parameter from Ubuntu" has **no protocol at all** behind it.
- **One agent at a time**: `connectToAgent()` in `bridge_daemon.cpp` is singular; discovery can list
  up to 32 but only the nearest is connected. Multi-robot is not done. (The relay exists precisely to
  merge two graphs into one, because the daemon can only connect to one.)
- **Security**: the client has `use_tls` / `tls_root_cert_pem` / `tls_target_name_override`, but the
  default is cleartext and **it has never been exercised**; there is no mTLS, no auth, no compression.
- **No version check**: `Ping` returns the peer's SDK version, and the client **never inspects it**.
  A version mismatch produces no warning — it just shows up as "some RPC behaves oddly".
- **Subscriptions have no queue and no drop statistics**: the client's subscribe loop is synchronous,
  so only HTTP/2's own flow control backstops a slow consumer; there is no backpressure policy and
  no backlog / drop counters. gRPC compression is not enabled either.
- **Network assumptions**: the Ubuntu machine must be in a scannable subnet; crossing subnets needs
  `--agent host:port` (there is no relay / gateway).

---

## D. How real the peer is

**This is the easiest thing to overlook.**

The earlier state: **no ROS2 on Ubuntu**; the peer was a hand-written Python simulation against
the proto (differential-drive car + synthetic lidar, 7 topics); the SDK's `RclcppBackend` had
**never been compiled or run**. The consequence was that the whole thing proved the **protocol and
the compatibility layer** were right, but said nothing about "what happens against a real ROS2
graph".

The state now: the robot runs **real ROS2 Jazzy** (`bridge/scripts/ubuntu_ros2_setup.sh`), the
`RclcppBackend` built with `ROS2_GRPC_SDK_WITH_ROS2=ON` runs
(`bridge/scripts/ubuntu_ros2_peer.sh`, port 50111), and the topic / service / action lists of a
real graph were measured end to end. Two bugs that **only appear against a real peer** were found
and fixed — an empty CDR body rejected by Fast CDR, and `rmw_serialize` receiving a null allocator.

**Still missing**: the measurements cover demo_nodes / `std_srvs` / `action_tutorials`;
**a real nav2 stack's services, topics and actions have not yet been measured against a real ROS2**.

Two simulation facts still stand: the simulator has no dynamics or sensor-noise model, and
`rosbag2` is still absent.

---

## E. slam_toolbox — what is missing is the periphery, not the core

The main mapping chain is complete (the sync / async nodes start, Karto's scan matching, pose
graph, loop closure and Eigen solver are all in, and archiving is exact down to the byte), and
"map → save → feed the saved map back to `map_server`" now has live evidence.

| Gap | Now | Cost |
| --- | --- | --- |
| **`merge_maps_kinematic.exe`** | Built; **both services were really called** (`add_submap` / `merge_submaps`), and both codecs are in the bridge's table. What remains is the **semantics**: `merge_submaps` relies on interactive-marker feedback to place submaps, and this shim has no rviz-like feedback source, so with zero submaps it is just an empty run | The "merge several submaps" path is unverified. The type plumbing is no longer the obstacle |
| **`map_saver`'s `system()` path** | slam_toolbox's own `map_saver.cpp` goes through `system("ros2 run nav2_map_server map_saver_cli ...")`, which always fails on Windows | Use `nav2\ros2_shim\build\map_saver_cli.exe`; no other cost |
| **Narrow passages / loop closure / long-term drift / large-map serialisation** | Untested | Map quality in those scenarios is unknown |

---

## F. Engineering

| Gap | Now | Consequence |
| --- | --- | --- |
| **CI** | `.github/workflows/cross-platform.yml` and `release.yml` are in place (Windows + Linux x86_64 + ARM64) | — |
| **Packaging / distribution** | `package.py` produces a zip / tar.gz per platform; CI publishes a Release on a tag with `SHA256SUMS.txt` | The Windows build needs its 88 DLLs; distribution is "copy the directory" — no installer, no upgrade path |
| **Performance baseline** | Only the shared-memory throughput (1M messages / 608 ms) | There is no quantified baseline for the end-to-end latency (Ubuntu lidar → Windows planning → back) |
| **Fault injection / stability** | **No test touches reconnecting**; peer restarts, LAN jitter and packet loss have never been tried, and there is no long soak run | The daemon's "a stream is not immortal, re-subscribe" logic **has never been verified**; whether a long run leaks memory or exhausts the slots is unknown |

---

## G. Things that have a binary but no evidence

These **compiled successfully** — some are even registered in the plugin table — but have never
really run (or only reached "created + activated"):

| Artefact | State |
| --- | --- |
| `assisted_teleop` / `drive_on_heading` | Created and activated inside `behavior_server`, with no caller |
| `merge_maps_kinematic.exe` | See section E: the service plumbing is verified; the "merge semantics" is not |
| `route_server` / `docking_server` | No graph file / no dock instance; ACTIVE is as far as it gets |
| MPPI's `AckermannMotionModel` / `OmniMotionModel` / 3 unused critics / `visualize` | Upstream's default set (8 critics) was used; the others were never exercised |
| The SDK's TLS channel | The switches exist; an encrypted connection has never been made |
| `/parameter_events` across the machine | The type **is in the codec table** and `ros2 topic echo` prints `node:` and the changes; the **cross-machine end-to-end run has not happened** (the proto still has no matching RPC — only the topic-forwarding path exists) |
| `bt_smoke.exe` / `tf_probe.exe` / `tf_listener_probe.exe` / `scan_path_probe.exe` / `amcl_probe.exe` / `parameter_events_probe.exe` | Bundled diagnostics, not part of the regression |

---

## H. If you could only do a few more things

Ordered by "capability returned per unit of effort":

1. **A command-line entry for cross-machine array codecs** (the `poses` / `viapoints` of
   `SmoothPath` / `FollowWaypoints`). It already works locally; across the machine only the array
   parsing in `build_goal` is missing.
2. **`nav2_theta_star_planner` / `nav2_constrained_smoother`** (one package each, shaped like the
   already-ported planner / smoother).
3. **Measure a real nav2 stack against a real ROS2** (the full set of topics, services, actions).
4. **Cross-machine parameter read/write** (add an RPC to the proto).
5. **The first real run of version control + CI.**
6. **The merge semantics of `merge_submaps`.**

**Not worth starting now**: porting RViz (the entire GUI stack, for the benefit of "being able to
see"), a full rclpy binding (redoing the Python ecosystem), Gazebo / physics simulation (the
project's own simulator is already enough to validate the chain), and porting `nav2_system_tests`
(depends on rclpy and a simulator — the lowest return).
