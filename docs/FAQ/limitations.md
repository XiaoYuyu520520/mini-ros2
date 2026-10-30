---
layout: default
title: 6 常见问题 · 功能限制与缺口
---

# 6.1 功能限制与缺口

<div class="overview">
<div class="label">本页总览</div>
<p>这一页只讲<strong>缺口</strong>。已经能跑的东西在 <a href="../Capability/overview">已实现能力</a>
里有证据，这里不重复。</p>
<p>每条写三件事：<strong>现在是什么样</strong>、<strong>缺什么</strong>、
<strong>缺了会有什么后果</strong>。</p>
</div>

## 6.1.1 一句话结论

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
5. **工程化那一层（CI / 安装包）** 不挡功能，挡的是「别人能不能复现」。

## 6.1.2 nav2：上游 46 个包，还缺 7 个

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

## 6.1.3 兼容层：ROS2 的表面还缺哪些

| 缺口 | 现状 | 缺了会有什么后果 |
| --- | --- | --- |
| **rclpy** | 没有 Python 绑定。`python/pymessages` 与 `py_interop` 是 ctypes + 生成的消息类，**不是 rclpy** | 依赖 rclpy 的东西全都用不了：`nav2_simple_commander`、上游 `nav2_system_tests`、任何「写个脚本节点」的用法 |
| **rosbag2** | 完全没有 | 不能录、不能放；复现一次跑只能重跑一遍。跨机时也没有「在 Windows 录 Ubuntu 的话题」这条路 |
| **launch / launch_ros** | 没有。启动靠 `bridge\windows\*.bat` 与 `bridge/scripts/run_*.py` | 上游的 `launch` 文件用不了（连带 remap、参数覆写、条件启动那一套语法）；换一个组合就要写 / 改一个 bat |
| **image_transport** | 只有 `point_cloud_transport` 的桩；没有 `image_transport` | `sensor_msgs/Image` 这类相机话题没有传输层，也没有压缩插件 |
| **RViz / rqt** | 没有，也不在计划里 | 「看得见」这件事只能用离线图或 CLI 顶替 |
| **tf 工具** | `tf2` / `tf2_ros` 库在，`tf_probe` / `tf_listener_probe` 是自带的诊断工具 | 没有 `static_transform_publisher`、`tf2_echo` / `tf2_monitor` 这些命令行工具 |
| **QoS** | 只有声明（可靠信道 / 历史深度落在本地环形缓冲）。**枚举的拼法有差异**：shim 写的是 `RELIABLE` / `BEST_EFFORT` / `KEEP_LAST`，上游写的是 `Reliable` / `BestEffort` / `KeepLast`。`nav2_extracted` 里**一个都没直接写枚举名**，所以现状编得过 | 没有 DDS 那套「同一话题不同 QoS 不匹配」的行为 —— 这是设计选择，不是缺陷。真正的遗留是**源码级拼法差异**：哪天有新代码按上游写 `rclcpp::ReliabilityPolicy::Reliable`，这里会编不过；补一组别名是几行的事 |
| **组件 / 组合** | `rclcpp_components` 只有 `register_node_macro.hpp`，没有 component container 的 `load_component` 运行路径 | 上游那种「一个进程装多个组件」的部署方式用不了；这里每个服务是一个 exe |
| **仿真时钟** | `use_sim_time` 只在那几个节点上打开；`rclcpp::utilities::set_use_sim_time()` 仍是空实现 | 没打开 `use_sim_time` 的节点里，「拿 `now()` 配消息戳」的代码会向未来外推 |
| **CLI 面** | 有 `topic` / `service` / `action` / `param` / `node list` / `interface list` / `bridge status` | 没有 `ros2 bag`、`ros2 run`、`ros2 launch`、`ros2 lifecycle`、`node info`、`interface show`、`topic bw` / `topic delay`。另外 `service call` 只认编解码表里的类型 |

**已经补上的（别再当缺口）**：参数服务、`/parameter_events`、话题 remap（`-r FROM:=TO`）、
`/rosout` 日志话题、8 位 PNG coder、位姿图存档、`-p` 的列表值。

## 6.1.4 跨机：边界是编解码表

三张表都在 `bridge\windows\src\` 里：`type_registry.cpp`（话题）、`service_registry.cpp`、
`action_registry.cpp`。

| 面 | 现在覆盖 | 表外会怎样 |
| --- | --- | --- |
| 话题 | **40 种** | 类型不在表里就**过不去**：`sensor_msgs/JointState` / `CompressedImage`、`std_msgs/Float64`…（本机 shm 上没这个限制：29 个消息包都生成了） |
| service | **38 种** | 表外还有 **45 种**跨不了机：`rcl_interfaces` 的 8 个参数服务、`robot_localization` 的 8 个、`map_msgs` 的 6 个、`dwb_msgs` 的 5 个、`geographic_msgs` 的 4 个…；本机 shm 上没这个限制 |
| action | **20 个** | **带数组字段的 goal**（`poses` / `viapoints` / `path`）`build_goal` 会明确拒绝，`ros2 action send_goal` 发不出去（本机 shm 上 `SmoothPath` / `FollowWaypoints` 已经端到端跑通） |

其它跨机缺口：

| 缺口 | 现状 | 后果 |
| --- | --- | --- |
| **proto 里没有的 RPC** | 一共 14 个 RPC，全是话题 / 服务 / action。**没有参数读写**、**没有 TF 查询**、**没有 bag** | 「Ubuntu 上直接改 Windows 节点的参数」这件事**连协议都没有** |
| **一次只对一个 agent** | `connectToAgent()` 是单数 | 多机器人场景没做（中继的存在就是为了把两张图合并成一张） |
| **安全** | 客户端有 `use_tls` 等三个开关，但默认明文、**从没验过** | 没有 mTLS / 鉴权 / 压缩 |
| **版本不比对** | `Ping` 会带回对端的 SDK 版本，但客户端拿到之后**没有任何检查** | 两端版本不一致时不会有提示，只会以「某个 RPC 行为不对」的形式表现出来 |
| **订阅没有队列，也没有丢弃统计** | 客户端的订阅循环是同步的 | 只有 HTTP/2 自己的流控兜底；没有背压策略、没有积压 / 丢弃计数。也没开 gRPC 压缩 |
| **网络假设** | Ubuntu 要在扫描可达的同一网段内 | 跨网段要 `--agent host:port`（没有中继 / 网关） |

## 6.1.5 对端真实性

<div class="callout note">
<span class="label">这一条最容易被忽略</span>
<p><strong>早期状态</strong>：Ubuntu 上<strong>没有装 ROS2</strong>，对端是按 proto 手写的 Python 仿真
（差分小车 + 合成雷达，7 个话题）；SDK 的 <code>RclcppBackend</code> <strong>从没编译过、也没跑过</strong>。
后果是：那套东西证明的是<strong>协议与兼容层是对的</strong>，但「接到真 ROS2 图上会怎样」
没有任何证据。</p>
<p><strong>现在的状态</strong>：机器人上装的是<strong>真的 ROS2 Jazzy</strong>
（<code>bridge/scripts/ubuntu_ros2_setup.sh</code>），<code>RclcppBackend</code> 编出来并在跑
（<code>ubuntu_ros2_peer.sh</code>，端口 50111），真图上的话题、服务、动作列表都对着它量过。
顺带撞出并修掉了<strong>两处只在真对端出现的 bug</strong> —— 空 CDR 体被 Fast CDR 拒读、
<code>rmw_serialize</code> 拿到零 allocator。</p>
<p><strong>还差的</strong>：量的是 demo_nodes / <code>std_srvs</code> / <code>action_tutorials</code>
那几条，<strong>真 nav2 栈上的服务、话题与动作还没对着一台真 ROS2 量过</strong>。</p>
</div>

**仿真仍然成立的两条**：仿真没有动力学与传感器噪声模型；`rosbag2` 仍然缺。

## 6.1.6 slam_toolbox：缺的是「周边」而不是「核心」

建图主链路是完整的，归档做到了逐母子，「建图 → 存图 → 把存下来的图喂回 `map_server`」
这一整圈有活的证据。

| 缺口 | 现状 | 代价 |
| --- | --- | --- |
| **`merge_maps_kinematic.exe`** | 编出来了；**两个服务真调过**（`add_submap` / `merge_submaps`），两个 codec 也补进了桥的表。剩下的是**语义**：后者要靠 interactive marker 的反馈来摆放子图，而这套 shim 里没有 rviz 那样的反馈源，零子图时它只是一趟空跑 | 多张子图**合并**这条路没验过。类型通路已经不是障碍了 |
| **`map_saver` 的 `system()` 路径** | `slam_toolbox` 自带的 `map_saver.cpp` 靠 `system("ros2 run ...")`，Windows 上必失败 | 用 `map_saver_cli.exe`，没有其他代价 |
| **窄道 / 回环 / 长时间漂移 / 大图序列化** | 没测 | 地图质量在这些场景下的表现未知 |

## 6.1.7 工程化

| 缺口 | 现状 | 后果 |
| --- | --- | --- |
| **CI** | `.github/workflows/cross-platform.yml` 与 `release.yml` 已就位（Windows + Linux x86_64 + ARM64 三平台） | —— |
| **安装包 / 分发** | `package.py` 按平台出 zip / tar.gz，CI 在 tag 上发 Release 并附 `SHA256SUMS.txt` | Windows 侧运行要带 88 个 DLL；分发是「复制目录」，没有安装器 / 升级路径 |
| **性能基线** | 只有共享内存那一条吞吐 | 端到端时延（Ubuntu 雷达 → Windows 规划 → 回传）没有量化基线 |
| **故障注入 / 稳定性** | 测试里**没有一条**碰断线重连；对端重启、LAN 抖动、丢包都没试过；也没有长时间 soak 跑 | 守护进程里那段「流不是永生的、要重订阅」的逻辑**没有被验证过**；长时间跑会不会漏内存、会不会把槽位耗光，也没有数据 |

## 6.1.8 有二进制、但没有证据

这些东西**编译通过了**、有的还注册进了插件表，但一次都没真跑过：

| 产物 | 状态 |
| --- | --- |
| `assisted_teleop` / `drive_on_heading` | `behavior_server` 里创建并激活过，没有调用方 |
| `merge_maps_kinematic.exe` | 服务通路已验，还剩下「合并语义」没验 |
| `route_server` / `docking_server` | 没有图文件 / 没有 dock 实例，只到 ACTIVE |
| MPPI 的 `AckermannMotionModel` / `OmniMotionModel` / 3 个未启用 critic / `visualize` | 用的是上游默认那一套（8 个 critic），一次都没调过 |
| SDK 的 TLS 通道 | 开关在，从没走过加密连接 |
| `/parameter_events` 跨机 | 类型**已经进了编解码表**，`ros2 topic echo` 打得出 `node:` 与参数变化；**跨机端到端还没实测**（proto 里仍然没有对应的 RPC） |
| `bt_smoke.exe` / `tf_probe.exe` / `tf_listener_probe.exe` / `scan_path_probe.exe` / `amcl_probe.exe` / `parameter_events_probe.exe` | 自带的诊断工具，不是回归的一部分 |

## 6.1.9 如果只能再做几件事

按「每单位功夫能取回多少能力」排：

1. **跨机数组 codec 的命令行入口**（`SmoothPath` / `FollowWaypoints` 的 `poses` / `viapoints`）。
   本机已经跑通，跨机只差 `build_goal` 里数组字段的解析。
2. **`nav2_theta_star_planner` / `nav2_constrained_smoother`**（各一个包，形状与已移植的
   规划器 / 平滑器相同）。
3. **真 nav2 栈对着一台真 ROS2 量一遍**（话题、服务、动作全套）。
4. **跨机参数读写**（proto 加 RPC）。
5. **CI 的第一次真跑**。
6. **`merge_submaps` 的合并语义**。

**不建议**现在去碰的：RViz 移植（整个 GUI 栈，收益只是「看得见」）、rclpy 全量绑定
（要重做一遍 Python 生态）、Gazebo / 物理仿真、`nav2_system_tests` 移植（依赖 rclpy + 仿真器，
性价比最低）。

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="../Capability/validation">5.2 验收与实测数据</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="troubleshooting">6.2 故障排查</a></div>
</div>
