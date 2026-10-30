# 01 · 架构

## 组件

| 组件 | 位置 | 职责 |
| --- | --- | --- |
| 仿真世界 `world.py` | `bridge/ubuntu/ros2_bridge_sim` | 24 m × 24 m 静态地图（0.05 m 栅格）、矩形障碍、360 线射线雷达 |
| 仿真小车 `sim.py` | 同上 | 差速运动积分，发布 `/odom` `/scan` `/tf` `/tf_static` `/map` `/clock`，订阅 `/cmd_vel` |
| gRPC agent `agent.py` | 同上 | 实现 `ros2bridge.Ros2Bridge`：`Ping` / `ListTopics` / `GetTopicInfo` / `Subscribe`(流) / `Publish`，原样转发 CDR 字节 |
| gRPC 通信 SDK | `gRPC/ros2_gRPC_sdk` | 发现、连接、keepalive、CDR 编解码、客户端/服务端封装 |
| 守护进程 `bridge_daemon.cpp` | `bridge/windows` | 扫描发现 → 持久连接 → 下行镜像 → 本地上行转发；维护图注册表 |
| 转码表 `type_registry.cpp` | 同上 | 38 种消息的 CDR ↔ 共享内存结构体双向转换（逐字节一致，证据 48） |
| ROS2 兼容层 | `nav2/ros2_shim` | `rclcpp` / `rclcpp_action` / `tf2` / 消息结构体的 Windows 实现，后端是共享内存 |
| 参数文件加载 | `nav2/ros2_shim/src/params_file.cpp` | `--params-file`：把上游 YAML 变成 `rclcpp::Parameter` 覆盖项（通配块、命名空间、嵌套展平、数组、双键登记） |
| 共享内存传输 | `nav2/shm_transport` | 命名共享内存环形缓冲（多槽、CRC、序号、写者唯一） |
| `ros2` CLI | `bridge/windows/tools/ros2_cli.cpp` | `ros2 topic ...` 等命令，读图注册表 + 共享内存 |
| nav2 | `nav2/nav2_extracted`（原样，只有一处例外：`nav2_mppi_controller/tools/parameters_handler.hpp` 的两行 `value_to_string()`，证据 30）+ `nav2/ros2_shim`（适配） | `nav2_costmap_2d`、`nav2_navfn_planner`、`nav2_planner`、`nav2_controller`、`nav2_bt_navigator` 等 |

## 下行链路（Ubuntu → Windows）

1. **发现**：守护进程对本地各子网的 `--ports`（默认 50051）做一次 TCP 快速扫描，
   命中的端口用 `Ping` 握手确认（`ros_distro=sim`、agent 版本回显）。
   实测 504 个地址的扫描 + 握手在 1 s 量级完成。
2. **持久连接**：握手成功后建立一个 gRPC 通道（HTTP/2 + keepalive），
   之后所有 `Subscribe` 流、`Publish` 调用都复用这条通道，不再重新发现。
3. **订阅**：`ListTopics` 拿到远端话题表，按 `--mirror` 过滤后，
   每个话题开一条 `Subscribe` 流（默认全量镜像）。
4. **落盘到共享内存**：流上收到的是**原始 CDR**（含 4 字节 encapsulation header）。
   `ros2_bridge::cdr_to_shim()` 按类型把 CDR 转成兼容层结构体布局，
   再写进共享内存通道 `ros2shim<topic>`。
5. **注册**：同时调用 `ProcessRegistry::set_remote_topic(topic, type, "remote:<peer>")`，
   于是任何进程（包括 `ros2 topic list` 和 nav2 节点）都能看到这个话题及其来源。

## 上行链路（Windows → Ubuntu）

* 守护进程按 `--uplink`（默认 `/cmd_vel`、`/goal_pose`、`/initialpose`，再加上 nav2 回话的
  `/plan` 与 `/behavior_tree_log`）为每个话题起一个转发线程，
  并且**由它自己创建、持有**那条共享内存通道（`create` 而不是 `open`）。命名共享内存是
  `Local\` 内核对象，只活在「有人持有句柄」期间：`ros2 topic pub --once /cmd_vel` 自己建一条
  通道、发一条、进程退出，通道随之消失，转发线程即使随后打开也已经无消息可读。让所有者是
  常驻的守护进程，一次性发布才不会再被丢掉；
* 转发线程订阅该通道；
* 本地没有发布者时也会先向 agent 查询类型并预注册，所以「nav2 之后才第一次发 `/cmd_vel`」也能工作；
* 收到本地消息后 `shim_to_cdr()` 转回 CDR，通过 `Publish` RPC 送到 agent，
  由 agent 注入 Ubuntu 侧的话题图，仿真小车随即动起来。

## 动作链路（action）

上游一个 action 在两个 topic（`_feedback`、`_status`）和两个 service（`_send_goal`、
`_cancel_goal`）之上搭出来；这里没有 DDS，所以把同一件事摊成 **6 条共享内存通道**，
每条都是「一个写者 + 任意多个读者」，名字就是 action 名加后缀：

| 通道 | 写者 | 读者 | 载荷 |
| --- | --- | --- | --- |
| `<action>/_goal` | 客户端 | 服务端 | 8 字节小端 goal id + CDR→兼容层的目标体 |
| `<action>/_goal_ack` | 服务端 | 所有客户端 | id + 1 字节（0 = REJECT，1 = ACCEPT） |
| `<action>/_cancel` | 客户端 | 服务端 | id（无体） |
| `<action>/_cancel_ack` | 服务端 | 所有客户端 | id + 1 字节（0 = REJECT，1 = ACCEPT） |
| `<action>/_feedback` | 服务端 | 所有客户端 | id + 反馈体 |
| `<action>/_result` | 服务端 | 所有客户端 | id + 1 字节 `ResultCode`（0 UNKNOWN / 1 SUCCEEDED / 2 ABORTED / 3 CANCELED）+ 结果体 |

几条必须守住的约定，都是踩过之后写下来的：

* **id 代替 UUID**。上游用 16 字节 UUID 认领 goal，这里用一个进程内自增计数器发出的
  `uint64_t`，`make_uuid(id)` 再把前 8 字节摊成 UUID 给 `rclcpp_action` 的 API 看。
  计数器是**进程级 static**，不是每个客户端一个：同一个 action 上两个客户端各自从 1 数起
  的话，服务端就分不清两条消息是谁的。跨进程不冲突，因为计数只在「发给同一个服务端」的
  客户端之间共享。
* **每个客户端/服务端各自一个读游标**（`Node::get_private_channel` 每次都 `open()` 出一个
  新对象，而不是像 `get_channel` 那样把同一个对象递给所有人）。否则 `bt_navigator` 里
  「自己发给自己的客户端」（`NavigateToPoseNavigator::self_client_`，接收 `/goal_pose`
  时才发目标）会把另一个客户端等的 ack 先读走。
* **同一条话题在一个节点里也只有一个读游标**（`Node::get_channel` 把同一个对象递给所有要
  这个名字的人）。这对订阅是对的 —— 一个节点、一条话题、一个回调 —— 但**在一个节点里对同一条
  话题建两个订阅就会互相抢读**：谁先 poll 谁把消息吃掉，另一个永远收不到。上游的代码里有
  这种形状（`enable_stamped_cmd_vel` 打开时，同一话题上会同时挂 `Twist` 与 `TwistStamped`
  两个订阅），所以 `config/nav2_shim_overrides.yaml` 把它钉成 `false`，
  `nav2_waypoints.cpp` 也只听纯 `Twist` 的 `/cmd_vel`。
* **构造时 `seek_to_latest()`**：共享内存段是内核对象，写它的进程退出之后里面的
  旧消息还在。客户端在构造时把 `_goal_ack`/`_cancel_ack`/`_feedback`/`_result` 四条的游标
  推到队头，服务端把 `_goal`/`_cancel` 推到队头 —— 不然新起的进程会去回答上一轮留下的
  goal（而那时它还没 active，于是回一个 REJECT）。唯一例外是 `transient_local` 的
  latched 语义，它用 `seek_to_latched()`：游标停在队头**前一格**，下一次读正好拿到最后
  一条，也就是「订阅晚于发布仍然要收到」的那种。
* **同一次 `poll()` 里被 ack 的 result 要推迟一拍再交付**：客户端的一次
  `spin_until_future_complete` 可能把 ack 和 result 一起收走 —— 本机 server 就在同一个进程里
  时几乎必然如此（`waypoint_follower` → `bt_navigator` 正是这样）。立刻交付的话，
  `bt_action_node` 那种「结果早于 goal response 到达」的调用方会把它当成上一个目标的结果
  丢掉，这条 goal 就再也没有下文（树停在 `ComputePathToPose=RUNNING` / `FollowPath=IDLE`）。
  `Client::poll()` 因此把这种 result 放进 `deferred_results_`，下一次 `poll()` 再交付，
  `bt_action_node` 也改成收下早到的结果。根因与连跑数据见 `03-validation.md` 证据 26。
* **结果必须"发出去"**：`Server::publish_result()` 写完通道就把 handle 从表里摘掉；
  取消也要回一个结果（上游同样如此），不然调用方要等满整个超时。
* **可观测性**：`set ROS2_SHIM_ACTION_TRACE=1` 之后，六条通道上的每一次收发都往 stderr
  打一行 `[act CLIENT|SERVER pid=... t=...] <action> <事件>`。冷启动排查就是靠它：
  把每个节点的 `.log` 按时间排一遍，能直接看出「谁发了、谁没收到」。每次发消息还会检查
  `ShmChannel::publish()` 的返回值，超过槽大小的载荷会打一行
  `[act !! ] publish failed: N bytes > slot_size M`（这条**不受开关控制**，丢消息永远是 bug）。

跨机的那一半在守护进程里：`ListActions` / `GetActionInfo` 把 Ubuntu agent 的 action 表
并进图注册表，`SendActionGoal` / `ActionFeedback`(流) / `GetActionResult` /
`CancelActionGoal` 四个 RPC 做转发，codec 表（`bridge/windows/src/action_registry.cpp`）
负责 CDR ↔ 兼容层的双向转码。表里现在是 **20 个** action：仿真自己的 `Drive`，
加上 `nav2_msgs` 里**每一个 `.action` 文件**（19 个，见证据 61），并且每个类型都带一份
逐字段打印器（`ros2 action send_goal` 打的就是它）—— 别的 action 要跨机，仍得先给它加一份
codec。

## 共享内存与图注册表

* 通道名：`ros2shim` + 话题名（可选前缀 `NAV2_SHM_NAMESPACE`，见下）；槽大小按话题名决定
  （含 `map`/`costmap`/`voxel` 的用 8 MB，其余 2 MB；名字里带 `info` 的按小尺寸算，见下），
  槽数量与环境变量 `NAV2_SHM_SLOT_COUNT` 等一起由 `bridge_env.bat` 统一，
  否则读写双方会看到不同几何。
  这个尺寸是**承诺**不是上限：页文件支撑的 section 按整段提交，所以给一条没人发消息的话题
  留的槽，是别的进程再也用不到的内存。64 MB × 8 槽曾让每条 `map`/`costmap` 通道占 512 MB、
  `planner_server` 单独占 6 GB，表现出来是 `bt_navigator` 里一个 `std::bad_alloc`。
  按名字判断大小只能算近似（名字不是类型：`/keepout_costmap_filter_info` 装的是几个数），
  所以带 `info` 的名字不算网格话题。
  节点自己发的两条 —— `/parameter_events` 与 `/rosout`（`Node` 构造时建的 publisher，
  用户没有要求过；分别见 `03-validation.md` 证据 39 与证据 50）—— 固定走 **256 KB**，
  不受上面那条名字规则影响：`Log` 的 `msg` 与参数事件的长度都不受控，
  走默认小槽会把消息截断。
* 环形缓冲：单写者 + 多读者，读者各自持有游标；落后太多会丢最旧的消息（`dropped` 计数）。
  「单写者」是硬约定，不是性能取舍：守护进程镜像 `/scan` 时，本机就不能再有另一个进程发 `/scan`。
  要让两套栈同时活着（例如一边跑 `ctest`、一边连着 Ubuntu 车），给其中一套加
  `NAV2_SHM_NAMESPACE` —— 通道名变成 `ros2shim<NAMESPACE><topic>`，两边各写各的。
* 图注册表：每个进程在 `%TEMP%\nav2_shim_graph\<pid>.graph` 写一份自己的节点/话题表，
  读者合并所有文件并清理已退出进程的残留。这就是 Windows 上没有 DDS 发现却还能
  `ros2 topic list` 的原因。远端话题以 `remote:<host:port>` 作为 source 出现。
  重写一份图是**就地替换**（`MoveFileEx(REPLACE_EXISTING)`，有人拿着文件时才走
  `ReplaceFileW`），不先删旧文件：先 `remove` 再 `rename` 会留出一个名字不存在的窗口，
  落在窗口里的读者会把一个正在运行的节点读成「没有」，而且失败是安静的。
  读者侧用 `FILE_SHARE_DELETE` 打开，并对**任何**打开失败重试（含 `ERROR_FILE_NOT_FOUND`）——
  被等一会儿可以，被说错不行。写的一侧**不丢**：一次被拒绝的交换不是终点 —— 交换失败时
  `flush_locked()` 置一个 `dirty_` 标记、交给一个重刷线程**重新写一份 `.tmp` 再换**
  （原地重试同一个 install 不成立：`ReplaceFileW` 失败会把 `.tmp` 带走），所以「注册完就不再改图
  的节点」也不会永远停在旧内容上；线程只在 `dirty_` 时醒来，第一次就成功的路径不花额外代价。
  回归测试见 `ros2_shim_graph_rewrite`（证据 18）与 `ros2_shim_graph_rewrite_cross_process`
  （真起一个子进程按住文件、放手后自己落地，见 `04-gaps.md` 第十六节）。

## 转码正确性怎么保证

`bridge/tests/generate_fixtures.py` 从兼容层的头文件里**解析出 307 个结构体布局**（独立于 C++ 的
第二实现），再用 `rosbags`（ROS2 Jazzy 的 CDR 规则）生成 17 个话题的样本消息，分别写出
`*.cdr` 与 `*.shim`。`bridge_cdr_check` 断言：

```
cdr_to_shim(cdr) == shim        shim_to_cdr(shim) == cdr        cdr -> shim -> cdr 往返
```

结果是 35 个 fixture、105 组字节比较、0 失败 —— 即转码在两个方向上都是逐字节精确的（17 / 51 →
35 / 105：证据 48 把话题类型从 19 加到 38）。

## nav2 侧现状

`nav2/ros2_shim` 已经能编译并跑通魔改 nav2 的 `nav2_costmap_2d`、`nav2_navfn_planner`、
`nav2_planner`、`nav2_controller`、`nav2_bt_navigator`、`nav2_map_server`、`nav2_behaviors`、
`nav2_smoother`、`nav2_waypoint_follower`、`nav2_velocity_smoother`、`nav2_collision_monitor`、
`nav2_mppi_controller` 等包，`ctest` **29/29** 通过（48.27 s，2026-09-17 晚；这里原来写的 22/22 是更早一轮的数字）。

端到端 demo `nav2/ros2_shim/apps/nav2_bridge_demo.cpp` 用真实的 nav2 代码完成闭环：

1. 订阅 `/map`（远端地图）与 `/scan`（雷达）与 `/odom`（位姿）；
2. 用雷达做射线投射，维护一张占据栅格（射线经过的格子记为空闲，命中点记为占据）；
3. 与静态地图合并，用 chamfer 距离变换 + nav2 `InflationLayer` 的代价模型做膨胀；
4. 调用 `nav2_navfn_planner::NavFn` 规划（`calcNavFnDijkstra` + `calcPath`，与
   `NavfnPlanner::makePlan` 的调用序列一致），把 `/plan` 发到共享内存以便 `ros2 topic echo /plan` 观察；
5. 纯跟踪控制器按 10 Hz 发布 `/cmd_vel`，守护进程转发给 Ubuntu 小车；
6. 用 `/odom` 判定是否到达目标并输出 JSON 报告。

上面这条是**自己接的**闭环，用的都是 nav2 的库、不是 nav2 的服务器。上游那套服务器本体同样
能跑：`nav2/ros2_shim/apps/` 下的 `planner_server` / `controller_server` / `bt_navigator` /
`lifecycle_manager` 等九个 `main` 读上游的 `nav2_params.yaml`，行为树用的是上游 XML 的副本，
`bridge\windows\nav2_stack.bat` 一条命令按「AMCL → 三个服务器 → lifecycle_manager」的顺序
把它们拉起来，再由 `nav2_goal` 发一个 `navigate_to_pose`。三种角色的起点不同，所以顺序在这里
是硬约束：服务器起来是 unconfigured 的，靠管理器推到 active；而管理器推得动的前提是两个 costmap
能在 `on_activate()` 里解析出 `map -> base_link`，那条链的第一段（`map -> odom`）只有 AMCL 能给。
三次连跑的记录在 `03-validation.md` 证据 14。

**插件注册表的键要带命名空间**（这是 `header-only` 的 `include/pluginlib/class_loader.hpp`
里一条不显眼、但踩过一次的规则）：上游 pluginlib 读插件 XML，`name` 是配置里查的、
`type` 只是被构造的；这个兼容层不读 XML，注册与查找都按导出的拼写来。表里的键因此
**不能用短名**——`dwb_critics::PathAlignCritic` 与 `mppi::critics::PathAlignCritic` 是两个类
（`PreferForwardCritic` / `TwirlingCritic` 同样），短名做键就会互相覆盖，而覆盖掉的那个被查出来
以后是当**另一个基类**的指针用的（工厂函数里 `static_cast<Base *>` 的 `Base` 是注册时的基类），
虚调用直接跳进别的函数 —— 症状是段错误，不是一条能读的报错。查找顺序是
「带命名空间的拼写 → `normalize()` 之后」，见 `detail::lookup_keys()` /
`detail::find_factory()`，用例在 `tests/plugin_names.cpp`，排障过程在
`03-validation.md` 证据 30。另一条相关的例外：`trajectory_validators.xml` 里那个
`name` 与 `type` 不同的类，只能写一张一行的映射表（`detail::xml_name_alias()`）。

## 生命周期与关停顺序

关停顺序在这套代码里不是细节，而是「能不能优雅退出」的分界线。

nav2 把自己要做的收尾放在 rcl 的 pre-shutdown 回调里，而不是析构函数：`nav2::LifecycleNode`
构造时注册一个回调，里面跑 `runCleanups()`（ACTIVE 就先 `deactivate()`，INACTIVE 再
`cleanup()`）；`nav2::LifecycleManager` 也注册一个，用来丢掉 bond 表和汇合服务线程。
上游能这么写，是因为 `rclcpp::shutdown()` 会关 context，而关 context 会触发这些回调。

兼容层因此必须补上同样的语义（`nav2/ros2_shim/include/rclcpp/context.hpp`）：

* **一个进程一个 Context**（`Context::get_global()`）。原来每个 `NodeBaseInterface` 各带一个
  私有 Context，`rclcpp::shutdown()` 谁也够不着，回调注册了等于没注册。
* **`remove_pre_shutdown_callback()` 真的摘除**。节点析构时会摘掉自己的回调，摘不掉就会在
  下一次 `rclcpp::shutdown()` 里调到已经释放的对象。
* **`rclcpp::shutdown()` 先跑回调、再清 `ok` 标志**，顺序与上游一致，让收尾代码里读
  `rclcpp::ok()` 的组件仍看到它期待的世界。

为什么非做不可：`Costmap2DROS` 有一个 `std::unique_ptr<std::thread>` 的地图更新线程，只在
`on_deactivate()` 里 join。如果关停时节点还是 ACTIVE，那么派生类成员（那个线程对象）会先于
基类析构（`~nav2::LifecycleNode()` 里的 `runCleanups()`）销毁，线程在 joinable 状态下析构
会直接 `std::terminate` —— 曾经的 `planner_server` / `controller_server` 就是这个下场。
细节与实测见 `03-validation.md` 证据 12。

## 已知限制

* **动作层已经端到端跑通，但覆盖仍然有限**：`bt_navigator` 的 `compute_path_to_pose` /
  `follow_path` 与外部 `nav2_goal` 的 `navigate_to_pose` 都在共享内存上来回；
  `bridge_action_roundtrip` 覆盖 goal / 拒绝 / 取消 / 反馈 / 结果 / 未知 target 六种情形，
  外加**编解码表里每一行的字段与打印器**（**373 项断言**，见 `03-validation.md` 证据 16 / 61）。
  仍然没有覆盖的是：多目标并发下的缓存淘汰（`completed_` 只留 16 条）、
  `navigate_through_poses` 的端到端、以及**跨机 20 个 codec 里带数组字段的那几条**
  （`poses` / `viapoints` / `path` / `goals`：本机跑通了，跨机没有，见证据 22 / 24）。
* **action 的环形缓冲只有 8 个槽**。`_feedback` 在跟随时是 ~40 Hz，也就是约 190 ms 的
  缓冲：任何一次超过 190 ms 的调度延迟都会让读者跳过（`dropped` 计数）中间那些反馈。
  反馈丢几条无害，但这个「慢读者静默丢消息」的性质是这套传输的固有性质，值得记住。
  客户端自己**没有超时**（和上游一致），所以真丢了一条 ack/result，表现就是调用方一直等
  —— 排查手段就是上面那条 `ROS2_SHIM_ACTION_TRACE`。
* **一条话题名只对应一个通道、一个节点只对应一个读游标**：相对名先解析成绝对名再查表，
  所以 `scan` 与 `/scan` 落到同一条通道（这是想要的：nav2_amcl 写的是相对名，
  桥镜像进来的是绝对名）；但同一个节点里对同一条话题建两个订阅，两个订阅共用一个游标、
  互相抢读。**不要在同一个节点里订阅同一条话题两次** —— `enable_stamped_cmd_vel`
  被钉成 `false` 就是为了避开这个形状（`02-running.md` 的环境变量表里有它的位置）。
* **短名查找在两个包之间有同名类时是不确定的**：带命名空间的查找是准的
  （见上文的插件注册表那一段），但**只写短名**去查仍然落在最后注册的那一个上，而注册顺序
  取决于静态初始化的顺序，跨翻译单元未定义。nav2 自己的参数文件写的是全名
  （`mppi::critics::CostCritic`、`dwb_critics::RotateToGoalCritic`），所以走不到这条路；
  自己写插件名时也别省命名空间。
* **雷达栅格只增不减**：demo 的占据栅格不会把已经观测到的障碍改回空闲（静态世界下无影响，
  动态障碍需要引入清理/衰减）。
* **参数文件接在每一个 runner 上**：`slam_bridge_node` / `amcl_bridge_node` 与所有 nav2
  server（`apps/*_node.cpp`，跑法见 `ros2_shim/server_runner.hpp`）都接受 `--params-file` /
  `-p NAME:=VALUE`（见 `03-validation.md` 证据 6）。这也是「一条命令起上游那套服务器」的前提：
  参数按出现顺序生效，所以 `-p` 必须排在 `--params-file` 之后。
* **SLAM 与 AMCL 都已接上**：`slam_toolbox` 2.8.5 与 `nav2_amcl`（来自 navigation2 1.5.1，
  两者都在 humble 之后）都已移植进兼容层并编成
  Windows 静态自包含 exe，直接吃共享内存里的 `/scan` + `/tf`（AMCL 还要 `/map`），位姿图优化、
  回环检测与粒子滤波定位都能工作（跑法见 `02-running.md` 第 6、7 节，实测见 `03-validation.md`
  证据 4、5）。两个 node 在 configure/activate 之前都会等 `/scan` 的 `frame_id` 出现在 latched
  的 `/tf_static` 里，避免冷启动丢帧。
* **地图的持久化已经接上**：`nav2_map_server` / `map_saver` / `map_saver_cli` 都在
  （`apps/map_*_node.cpp`），图片格式是自己实现的 PGM（`include/Magick++.h` 替掉上游对
  GraphicsMagick 的依赖），所以「建图 → 存成 PGM + YAML → 再加载 → AMCL 定位」整条链在
  Windows 上闭环（`03-validation.md` 证据 15）。位姿图存档（`serialize_map` /
  `deserialize_map`）也已经验过（证据 41）：`-p map_file_name:=` 与
  `ros2 service call slam_toolbox/deserialize_map` 两个入口都能把存下来的位姿图读回来，
  所以 `slam_toolbox` 的 **localization / lifelong 模式**要的那一步不再是空的。
  顺带修掉的两个兼容层缺陷（Karto 归档在 UCRT 上按文本模式打开、`rclcpp::Service` /
  `rclcpp::Client` 漏掉 `seek_to_latest()`）记在同一条证据里，后者影响所有「重启同名服务」的路径。
* **nav2 的服务器本体已经跑通**：`apps/*_node.cpp` 下九个 `main`（`amcl_bridge_node`、
  `planner_server`、`controller_server`、`bt_navigator`、`behavior_server`、`smoother_server`、
  `waypoint_follower`、`lifecycle_manager`，外加旁路的 `velocity_smoother` / `collision_monitor`）
  读上游的 `nav2_params.yaml`，`bridge\windows\nav2_stack.bat` 一条命令把前七个拉到 active，
  再由 `nav2_goal` 发出 `navigate_to_pose` 把车开过去（实测见 `03-validation.md` 证据 14）。
  `bt_navigator` 的行为树是上游 XML 的副本（`nav2\ros2_shim\config\bt\*.xml`，目录由
  `NAV2_SHIM_BT_DIR` 指定）：末尾的恢复轮盘 `<Spin>` / `<Wait>` / `<BackUp>` 保留着，因为
  `behavior_server` 已经移植（证据 21）；行为树是**激活时**建的，叶子对应的 action server 不在
  就整个 `bt_navigator` 都激活不了——所以 `lifecycle_manager` 的 `node_names` 必须列上
  `behavior_server`（漏掉时的报错见证据 21）。导航器插件靠静态注册而不是 pluginlib 拿到
  （CMakeLists 里 whole-archive 那段注释解释了为什么）。栈要先有 AMCL 在发 `map -> odom`，
  否则两个 costmap 会在 `on_activate()` 里等 `base_link -> map` 等到超时。
* **时间基准**：Ubuntu 仿真把消息头的时间戳写成**仿真时间**（`sim.py` 的
  `clock_stamp()`，从 0 开始），`/clock` 也是它，由守护进程镜像进共享内存。
  Windows 侧节点打开 `use_sim_time` 后 `now()` 跟随 `/clock`
  （`rclcpp/clock.hpp` 的 `set_sim_time()`、`node.hpp` 的 `follow_clock_topic()`），
  `nav2_stack.bat` 起的节点（`amcl` / `planner_server` / `controller_server` /
  `bt_navigator` / `behavior_server` / `smoother_server` / `waypoint_follower`）都在这个配置下跑
  （见 `03-validation.md` 证据 19）。**没打开** `use_sim_time`
  的节点 `now()` 是系统墙钟（1.79e9 s），与消息戳差 56 年——这类节点里凡是把
  `now()` 和消息戳配在一起的代码都在向未来外推；栈过去一直能跑，是因为被走到的那些
  查询用的是**消息自带的戳**（`tf2_ros::MessageFilter`），而 TF 也在同一条时间轴上。
  最外层的 `rclcpp::utilities::set_use_sim_time()` 仍是空实现（没人用它，节点走的是参数）；
  独立的 `amcl_demo.bat` / `slam_demo.bat` 没有改：`slam_toolbox` 与 `nav2_amcl` 的带戳动作
  都取扫描自带的时间戳，两种时钟下都对。
* **地图图像有两个 coder：PGM 和 8 位 PNG**（`include/Magick++.h` 替掉上游对 GraphicsMagick
  的依赖）。PGM 进 P5/P2、出 P5；PNG 是 8 位，认灰度 / RGB / RGBA / 灰度+alpha / 调色板五种颜色
  类型和五种行过滤器，出灰度或灰度+alpha，解压用工具链自带的 zlib（`-static` 一起进 exe）。
  `bmp` / `jpeg`、16 位或交错的 PNG 仍然没有 coder，会以一条点出格式名的错误失败，所以
  `map_saver` / `map_saver_cli` 的 `--fmt` 实际可用的值是 `pgm` 和 `png`。`mode: scale` 要 PNG
  才存得下透明通道——上游 `checkSaveParameters()` 在那个模式下默认就是 `png`，`slam_toolbox`
  自带的配置里 `image_format` 也是 `png`（`03-validation.md` 证据 15 / 35 / 36）。
* 上游 `nav2_map_server` 的 vector object server（`vector_object_server`）**已经移植**
  （`apps/vector_object_server_node.cpp`，`03-validation.md` 证据 34）。它要的 `uuid/uuid.h` 是
  本兼容层自己带的一份（`nav2/ros2_shim/include/uuid/uuid.h`，实现那两处文件用到的九个 libuuid
  调用），不是系统库；`ros2_shim_uuid` 这条 ctest 盯着它。
* `slam_toolbox` 的 `map_saver.cpp` 走 `system("ros2 run nav2_map_server map_saver_cli ...")`
  （`third_party/slam_toolbox/src/map_saver.cpp:85` 与 `:95`），在 Windows 上仍然失败——它要的是
  一个真的 `ros2` CLI 加上 ament 的包查找，而这里 `ros2` 是替身。要存图就直接用
  `nav2\ros2_shim\build\map_saver_cli.exe`。
