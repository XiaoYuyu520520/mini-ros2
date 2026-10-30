---
layout: default
title: 架构 Architecture
---

# 架构 Architecture

> **[中文](#中文) · [English](#english)**

---

# 中文

mini-ros2 由三个子项目拼成。理解它们的边界，是读懂整个仓库的前提。

| 子项目 | 路径 | 角色 |
| --- | --- | --- |
| **胶水层** | `bridge/` | Windows 守护进程、`ros2` CLI、Ubuntu 仿真小车、跨机中继、入口脚本、验收文档 |
| **通信 SDK** | `gRPC/ros2_gRPC_sdk/` | gRPC 协议定义、客户端 / 服务端库、LAN 发现、CDR 编解码 |
| **兼容层 + 魔改 nav2** | `nav2/ros2_shim/` | 在 Windows 上造出 `rclcpp` / `rclcpp_action` / `tf2` 等接口，**原地编译上游 nav2 1.5.1** |

外加两块支撑：

| 子项目 | 路径 | 角色 |
| --- | --- | --- |
| 共享内存传输 | `nav2/shm_transport/` | 单写者多读者的环形缓冲 + 反射注册表；Windows / Linux 两套后端共用一份算法 |
| slam_toolbox 移植 | `nav2/slam_toolbox_port/` | 上游 slam_toolbox 2.8.5 的 Windows 移植 |

## 整体数据流

```
Ubuntu（真 ROS2 + 仿真）                          Windows
────────────────────────                          ──────
  真 ROS2 图 ──┐
               ├──gRPC──>  ros2_link 中继 ──gRPC──>  ros2_bridge_daemon.exe ──共享内存──> nav2 / ros2 CLI / 你的节点
  仿真图   ──┘             (relay.py)                  (单 peer 模型)                       (ros2_shim 兼容层)
                                                                                               │
  小车移动  <────────────────────gRPC───────────────────  <───────────────共享内存─────────────┘
```

## 三层分工

### 第一层：gRPC 跨机传输（`gRPC/ros2_gRPC_sdk`）

协议在 `proto/ros2_bridge.proto`，服务 `ros2bridge.Ros2Bridge` 共 14 个 RPC：

| RPC | 类型 | 用途 |
| --- | --- | --- |
| `Ping` | 一元 | 存活 / 版本握手 |
| `ListTopics` / `GetTopicInfo` | 一元 | 列话题、解析类型与端点计数 |
| `Subscribe` | 服务端流 | 推某话题的每条消息，直到取消 |
| `Publish` | 一元 | 发布一条序列化消息 |
| `ListServices` / `GetServiceInfo` / `CallService` | 一元 | 服务列表 / 类型 / 一次请求-响应 |
| `ListActions` / `GetActionInfo` | 一元 | 动作列表 / 类型 |
| `SendActionGoal` | 一元 | 投递目标（被接受或被拒） |
| `ActionFeedback` | 服务端流 | 反馈流，末尾一条终态结果 |
| `GetActionResult` / `CancelActionGoal` | 一元 | 稍后回来取结果 / 取消目标 |

**载荷是不透明的 CDR 字节**（含 4 字节封装头）+ 一个类型名 —— 传输层不需要认识消息结构，
所以对端没实现的类型也能原样过桥。服务端侧用可插拔的 `IRos2Backend`：
`RclcppBackend`（真 ROS2 图）、`MockBackend`（内存模拟，CI 用）。

**LAN 发现**（`discovery.cpp`）：枚举所有网卡 → 展开 CIDR → 并发 TCP 探活 → 用 gRPC `Ping`
确认并测 RTT → 按延迟升序排。**默认排除本机地址与整个 `127.0.0.0/8`** —— 所以守护进程和
agent 在同一台机器上时必须显式给地址（`ROS2_BRIDGE_AGENT=127.0.0.1:50051`）。

### 第二层：桥守护进程（`bridge/windows/src/bridge_daemon.cpp`）

一个常驻进程，做四件事：

**① 发现 + 持久连接**
对本地各子网的 `--ports`（默认 50051）做一次 TCP 快速扫描，命中的端口用 `Ping` 握手确认
（回显 `ros_distro=sim` 与 agent 版本）。握手成功后建立一个 gRPC 通道（HTTP/2 + keepalive），
之后所有 `Subscribe` 流与 `Publish` 都复用它，不再重新发现。
实测 504 个地址的扫描 + 握手在 1 s 量级完成。

**② 下行镜像**（Ubuntu → Windows）
`ListTopics` 拿到远端话题表 → 按 `--mirror` 过滤 → 每个话题开一条 `Subscribe` 流 →
流上是原始 CDR → `ros2_bridge::cdr_to_shim()` 按类型转成兼容层结构体布局 →
写进共享内存通道 `ros2shim<topic>` → 同时
`ProcessRegistry::set_remote_topic(topic, type, "remote:<peer>")` 注册。

**③ 上行转发**（Windows → Ubuntu）
按 `--uplink`（默认 `/cmd_vel`、`/goal_pose`、`/initialpose`，加上 nav2 回话的 `/plan` 与
`/behavior_tree_log`）为每个话题起一个转发线程。**由守护进程自己创建并持有**那条共享内存
通道 —— 命名共享内存是 `Local\` 内核对象，只活在「有人持有句柄」期间；`ros2 topic pub --once`
自己建的通道会随进程退出消失，一次性发布就丢了。

> 转发线程**订阅**通道、**本地没有发布者时也先向 agent 查询类型并预注册**，
> 所以「nav2 之后才第一次发 `/cmd_vel`」也能工作。

**④ 维护图注册表**
见下面「发现机制」。

### 第三层：ROS2 兼容层（`nav2/ros2_shim`）

`include/` 里把 ROS2 的 C++ 接口造出来：

| 目录 | 内容 |
| --- | --- |
| `rclcpp/` | 36 个头：`node.hpp` / `publisher.hpp` / `subscription.hpp` / `parameter_service.hpp` / `clock.hpp` / `context.hpp` / `qos.hpp` … |
| `rclcpp_action/` `rclcpp_lifecycle/` | action 客户端 / 服务端；生命周期节点 |
| `tf2/` `tf2_ros/` `tf2_geometry_msgs/` `tf2_sensor_msgs/` | TF 库与消息 |
| `pluginlib/` | 插件注册表（**键必须带命名空间**，见下） |
| `message_filters/` `bond/` `bondcpp/` `diagnostic_updater/` `interactive_markers/` | 上游依赖的替身 |
| `Poco/` `tbb/` `angles/` `uuid/` `Magick++/` | 第三方替身；`Magick++` 是 PGM + 8 位 PNG 图像编解码器 |

每个编译单元都被 `-include ros2_shim/prelude.hpp` 强制注入一层垫片 —— 这是「兼容层怎么骗过
nav2」的核心机制：上游 nav2 源码**一行没改**，缺什么就补什么。

上游 nav2 1.5.1 的源码放在 `nav2/nav2_extracted/navigation2-1.5.1/`，由
`nav2/ros2_shim/CMakeLists.txt:96` 起的 `nav2_package_sources()` **原地 glob 编译**：
它 glob 每个包的 `src/**.cpp` 与 `plugins/**.cpp`，自动丢掉自带 `main()` 的文件
（那些位置交给 `apps/`），并为每个编译单元注入垫片。

**唯一一处改了上游**：`nav2_mppi_controller/tools/parameters_handler.hpp` 的两行
`value_to_string()`。这处改动由 `nav2/patches/` 里的 patch + `verify_patch.py` 表达，
构建期校验 —— 换上游版本时不会悄悄丢掉。

## 发现机制：图注册表

没有 DDS，就没有 DDS 发现。每个进程在 `%TEMP%\nav2_shim_graph\<pid>.graph` 写一份自己的
节点 / 话题表；读者合并所有文件，并按 pid 判断注册者是否已退出（已退出的顺带清掉）。
这就是 Windows 上没有 DDS 却还能 `ros2 topic list` 的原因。远端话题以 `remote:<host:port>`
作为 source 出现。

**写的一侧不丢**：重写一份图是**就地替换**（`MoveFileEx(REPLACE_EXISTING)`），
不先删旧文件 —— 先 `remove` 再 `rename` 会留出一个「名字不存在」的窗口，落在窗口里的读者会把
一个正在运行的节点读成「没有」，而且失败是安静的。一次被拒绝的交换不是终点：
交换失败时置一个 `dirty_` 标记，交给重刷线程**重新写一份 `.tmp` 再换**。

## 共享内存传输

`nav2/shm_transport` 是本地总线：

| 特性 | 说明 |
| --- | --- |
| 通道名 | `ros2shim` + 话题名（可选前缀 `NAV2_SHIM_NAMESPACE`） |
| 拓扑 | **单写者 + 多读者**，读者各自持有游标 |
| 落后太多 | 丢最旧的消息（`dropped` 计数） |
| 槽几何 | 由 `NAV2_SHM_*` 环境变量决定，发布 / 订阅双方必须一致（`bridge_env.bat` 统一） |

平台差异收在 `ShmChannel::Impl` 里：

| 要同步的东西 | Windows | Linux / POSIX |
| --- | --- | --- |
| 通道本体 | `CreateFileMapping(Local\nav2shim_<名>_data)` | `shm_open` + `ftruncate` + `mmap` |
| 写者锁 | 命名互斥体 + 5 s 有界等待 | 段内 `pthread_mutex_t`（`PROCESS_SHARED` + `ROBUST`）+ `timedlock` |
| 唤醒读者 | 命名手工事件（`SetEvent` / `ResetEvent`） | 段内 `pthread_cond_t` **广播**（`sem_post` 只唤醒一个，不够用） |
| 段的生命周期 | 最后一个句柄关掉时内核销毁 | 段本身的 `flock` 共享锁：内核在进程死掉时收回，最后一把锁的人 `shm_unlink` |

两条踩过的坑（都有回归用例）：
**`EOWNERDEAD` 必须当成「锁拿到了」**（`ROBUST` 互斥体在持有者死掉时把锁交出来，
只有调用 `pthread_mutex_consistent()` 的人才能再把它放掉）；
**段的生命周期不能靠段内引用计数**（被 `kill -9` 的进程没人减，名字永远不回收）。

## action 怎么在共享内存上搭出来

上游一个 action 在两个 topic（`_feedback`、`_status`）和两个 service（`_send_goal`、
`_cancel_goal`）之上搭出来。这里没有 DDS，所以把同一件事摊成 **6 条共享内存通道**：

| 通道 | 写者 | 读者 | 载荷 |
| --- | --- | --- | --- |
| `<action>/_goal` | 客户端 | 服务端 | 8 字节小端 goal id + 目标体 |
| `<action>/_goal_ack` | 服务端 | 所有客户端 | id + 1 字节（0 = REJECT，1 = ACCEPT） |
| `<action>/_cancel` | 客户端 | 服务端 | id（无体） |
| `<action>/_cancel_ack` | 服务端 | 所有客户端 | id + 1 字节 |
| `<action>/_feedback` | 服务端 | 所有客户端 | id + 反馈体 |
| `<action>/_result` | 服务端 | 所有客户端 | id + 1 字节 `ResultCode` + 结果体 |

必须守住的约定（都是踩过之后写下来的）：

- **id 代替 UUID**。上游用 16 字节 UUID 认领 goal，这里用进程级 static 自增计数器发出的
  `uint64_t`，`make_uuid(id)` 再把前 8 字节摊成 UUID 给 `rclcpp_action` 的 API 看。
  计数器是**进程级**的，不是每个客户端一个 —— 否则服务端分不清两条消息是谁的。
- **每个客户端 / 服务端各自一个读游标**。`Node::get_private_channel` 每次 `open()` 出新对象，
  而 `get_channel` 把同一个对象递给所有要这个名字的人 —— 否则「自己发给自己的客户端」
  会把另一个客户端等的 ack 先读走。
- **同一条话题在一个节点里也只有一个读游标**。这对订阅是对的，但**一个节点里对同一条话题
  建两个订阅就会互相抢读**。上游就有这种形状（`enable_stamped_cmd_vel` 打开时同一话题上
  同时挂 `Twist` 与 `TwistStamped` 两个订阅），所以它被钉成 `false`。
- **构造时 `seek_to_latest()`**。共享内存段是内核对象，写它的进程退出后旧消息还在。
  不 seek 的话，新起的进程会去回答上一轮留下的 goal（而那时它还没 active，于是回一个 REJECT）。
- **同一次 `poll()` 里被 ack 的 result 要推迟一拍再交付**。本机 server 就在同一个进程里时
  几乎必然如此。立刻交付的话，「结果早于 goal response 到达」的调用方会把它当成上一个目标的
  结果丢掉，这条 goal 就再也没有下文。
- **可观测性**：`ROS2_SHIM_ACTION_TRACE=1` 让六条通道上的每次收发都往 stderr 打一行。

## 插件注册表：键必须带命名空间

上游 pluginlib 读插件 XML，`name` 是配置里查的、`type` 只是被构造的；这个兼容层不读 XML，
注册与查找都按导出的拼写来。表里的键**不能用短名** —— `dwb_critics::PathAlignCritic` 与
`mppi::critics::PathAlignCritic` 是两个类，短名做键会互相覆盖，而覆盖掉的那个被查出来以后
是当**另一个基类**的指针用的（工厂函数里 `static_cast<Base *>` 的 `Base` 是注册时的基类），
虚调用直接跳进别的函数 —— 症状是段错误，不是一条能读的报错。

查找顺序是「带命名空间的拼写 → `normalize()` 之后」，见 `detail::lookup_keys()` /
`detail::find_factory()`，用例在 `tests/plugin_names.cpp`。

## 生命周期与关停顺序

关停顺序在这套代码里不是细节，而是「能不能优雅退出」的分界线。

nav2 把收尾放在 rcl 的 pre-shutdown 回调里（而不是析构函数）：`nav2::LifecycleNode` 构造时注册
一个回调跑 `runCleanups()`。上游能这么写，是因为 `rclcpp::shutdown()` 会关 context，
而关 context 会触发这些回调。兼容层因此必须补上同样的语义：

- **一个进程一个 Context**（`Context::get_global()`）。原来每个 `NodeBaseInterface` 各带一个
  私有 Context，`rclcpp::shutdown()` 谁也够不着，回调注册了等于没注册。
- **`remove_pre_shutdown_callback()` 真的摘除**。节点析构时会摘掉自己的回调，
  摘不掉就会在下次 shutdown 里调到已释放的对象。
- **`rclcpp::shutdown()` 先跑回调、再清 `ok` 标志**，顺序与上游一致。

为什么非做不可：`Costmap2DROS` 有一个 `std::unique_ptr<std::thread>` 的地图更新线程，
只在 `on_deactivate()` 里 join。如果关停时节点还是 ACTIVE，派生类成员（那个线程对象）会先于
基类析构销毁，线程在 joinable 状态下析构会直接 `std::terminate` —— 曾经的
`planner_server` / `controller_server` 就是这个下场。

## Ubuntu 侧：仿真小车与中继

**仿真**（`bridge/ubuntu/ros2_bridge_sim/`，纯 Python）：24 m × 24 m 静态地图（0.05 m 栅格）、
矩形障碍、360 线射线雷达、差速运动积分。发布 `/odom` `/scan` `/tf` `/tf_static` `/map` `/clock`，
订阅 `/cmd_vel`。**没有任何动力学与传感器噪声模型** —— 确定性，同种子同命令可复现。

**中继**（`bridge/ubuntu/ros2_link/relay.py`）：真 ROS2 agent 与仿真 agent 是**两个各自独立的
gRPC 服务端**，协议一样但图不通（仿真图只活在 Python 进程的 `graph.Graph` 里，从没进过 DDS；
真 ROS2 图在 DDS 上，仿真 agent 看不见它）。中继同时扮演 gRPC 客户端与服务端，把两张图合并成
一张暴露给 Windows —— 守护进程是**单 peer 模型**，所以这是必须的。

中继还负责：

- **同名话题不混流**：赢家保住原名，输家改挂 `/real/<topic>` 或 `/sim/<topic>`（`--collision
  prefer-sim|prefer-real`），**不丢数据**，只是分开。
- **下发 `/cmd_vel` 路由给「消费它的那一侧」**，而不是「发布它的那一侧」—— 真 ROS2 侧由
  velocity smoother 发布，仿真侧只有小车订阅。路由错了，命令会进真机器人，仿真车一步不动。
- **action 的 goal_id 加前缀**（`sim:` / `real:`），否则两个上游各自发 uuid 可能撞，
  下游「follow 这个 goal」时无法无歧义地找回是哪个上游。

## 数据传输的正确性怎么保证

`bridge/tests/generate_fixtures.py` 从兼容层头文件里**解析出 307 个结构体布局**
（独立于 C++ 的第二实现），再用 `rosbags`（ROS2 Jazzy 的 CDR 规则）生成 17 个话题的样本消息，
分别写出 `*.cdr` 与 `*.shim`。`bridge_cdr_check` 断言：

```
cdr_to_shim(cdr) == shim        shim_to_cdr(shim) == cdr        cdr -> shim -> cdr 往返
```

结果是 35 个 fixture、105 组字节比较、0 失败 —— 转码在两个方向上都是逐字节精确的。

## 性能基线

共享内存**不是零拷贝**。一次发布有 4 次拷贝：

```
publish(msg) → serialize()      拷贝1 结构体→bytes
             → memcpy 进共享槽   拷贝2 bytes→slot
============== 进程边界（这里是共享，不是拷贝）==============
subscribe() → memcpy 出槽       拷贝3 slot→读者缓冲
            → deserialize()     拷贝4 bytes→结构体
```

准确说法是**零系统调用（zero-syscall），不是零拷贝**。省下的是内核 socket/UDP 栈。

实测（Release / 6 P-core，完整路径含 serialize/deserialize）：

| 载荷 | 线上大小 | 同进程 msg/s | 跨进程 msg/s | 延迟 p50 |
| --- | --- | --- | --- | --- |
| Twist | 48 B | 1.32 M | 1.40 M | 0.5 µs |
| Odometry | 709 B | 462 k | 483 k | 1.6 µs |
| LaserScan | 5813 B | 87 k | 88 k | 8.8 µs |

**跨进程 ≈ 同进程** —— 这是「共享内存本来就不为进程边界付钱」的最好证据。

---

# English

mini-ros2 is assembled from three sub-projects. Understanding their boundaries is the
prerequisite for reading the whole repository.

| Sub-project | Path | Role |
| --- | --- | --- |
| **Glue layer** | `bridge/` | Windows daemon, `ros2` CLI, Ubuntu simulator, cross-machine relay, entry scripts, docs |
| **Communication SDK** | `gRPC/ros2_gRPC_sdk/` | gRPC protocol, client/server libraries, LAN discovery, CDR codec |
| **Compatibility layer + patched nav2** | `nav2/ros2_shim/` | Reimplements `rclcpp` / `rclcpp_action` / `tf2` on Windows and compiles **upstream nav2 1.5.1 in place** |

Plus two supporting trees: `nav2/shm_transport/` (shared-memory ring buffer) and
`nav2/slam_toolbox_port/` (Windows port of slam_toolbox 2.8.5).

## End-to-end data flow

```
Ubuntu (real ROS2 + simulator)                    Windows
──────────────────────────                        ──────
  real ROS2 graph ──┐
                   ├──gRPC──>  ros2_link relay ──gRPC──>  ros2_bridge_daemon.exe ──shared memory──> nav2 / ros2 CLI
  simulator graph ─┘           (relay.py)          (single-peer model)                          (ros2_shim)
                                                                                                       │
  the car moves  <────────────────────gRPC──────────────────  <─────────────shared memory─────────────┘
```

## Layer 1 — gRPC transport (`gRPC/ros2_gRPC_sdk`)

The protocol lives in `proto/ros2_bridge.proto`; the service `ros2bridge.Ros2Bridge` has 14 RPCs:
`Ping`, `ListTopics`, `GetTopicInfo`, `Subscribe` (server streaming), `Publish`,
`ListServices`, `GetServiceInfo`, `CallService`, `ListActions`, `GetActionInfo`,
`SendActionGoal`, `ActionFeedback` (server streaming), `GetActionResult`, `CancelActionGoal`.

**Payloads are opaque CDR bytes** (with the 4-byte encapsulation header) plus a type name — the
transport never needs to know the message structure, so types the peer does not implement can still
cross the bridge untouched. The server side is a pluggable `IRos2Backend`: `RclcppBackend` (a real
ROS2 graph) or `MockBackend` (in-memory, used by CI).

**LAN discovery** (`discovery.cpp`): enumerate NICs → expand CIDR → concurrent TCP probe →
confirm with a gRPC `Ping` and time the RTT → sort by ascending latency. **This host and the whole
`127.0.0.0/8` range are excluded by default**, so when the daemon and the agent share a machine you
must give the address explicitly (`ROS2_BRIDGE_AGENT=127.0.0.1:50051`).

## Layer 2 — the bridge daemon (`bridge/windows/src/bridge_daemon.cpp`)

One long-running process doing four things:

**① Discovery + persistent connection.** A single TCP sweep of the local subnets on `--ports`
(default 50051); every hit is confirmed with a `Ping` handshake (echoing `ros_distro=sim` and the
agent version). After the handshake one gRPC channel (HTTP/2 + keepalive) carries every subsequent
`Subscribe` stream and `Publish` — no re-discovery. Measured: 504 addresses swept and handshaked in
~1 s.

**② Downstream mirroring (Ubuntu → Windows).** `ListTopics` → filter by `--mirror` → one
`Subscribe` stream per topic → raw CDR on the stream → `cdr_to_shim()` converts it to the
compatibility-layer struct layout → written into the `ros2shim<topic>` shared-memory channel →
and registered via `ProcessRegistry::set_remote_topic(topic, type, "remote:<peer>")`.

**③ Upstream forwarding (Windows → Ubuntu).** One forwarding thread per topic on the `--uplink`
list (default `/cmd_vel`, `/goal_pose`, `/initialpose`, plus nav2's `/plan` and
`/behavior_tree_log`). **The daemon creates and owns the shared-memory channel itself** — a named
shared-memory object is a `Local\` kernel object that only lives while somebody holds a handle; a
one-shot `ros2 topic pub --once` builds its own channel, and that channel dies with the process,
silently dropping the message.

> The forwarding thread **subscribes** to the channel and, when there is no local publisher yet,
> **pre-registers the type by asking the agent** — so "nav2 publishes `/cmd_vel` for the first time
> much later" also works.

**④ Graph registry maintenance.** See "Discovery" below.

## Layer 3 — the ROS2 compatibility layer (`nav2/ros2_shim`)

`include/` reimplements the ROS2 C++ interfaces: `rclcpp/` (36 headers), `rclcpp_action/`,
`rclcpp_lifecycle/`, the `tf2*` family, `pluginlib/`, `message_filters/`, `bond/`, `uuid/`,
`Magick++/` (a PGM + 8-bit PNG image codec), and more.

Every translation unit gets `-include ros2_shim/prelude.hpp` — this is the core of "how the shim
fools nav2": **upstream nav2 source is not modified at all**; whatever is missing gets patched in.

Upstream nav2 1.5.1 lives in `nav2/nav2_extracted/navigation2-1.5.1/` and is **glob-compiled in
place** by `nav2_package_sources()` at `nav2/ros2_shim/CMakeLists.txt:96`: it globs each package's
`src/**.cpp` and `plugins/**.cpp`, drops the files that carry their own `main()` (those become
`apps/`), and injects the prelude into every unit.

**The single upstream change**: two lines of `value_to_string()` in
`nav2_mppi_controller/tools/parameters_handler.hpp`. It is expressed as a patch in `nav2/patches/`
and verified at build time by `verify_patch.py`, so it cannot silently vanish on an upstream bump.

## Discovery — the graph registry

Without DDS there is no DDS discovery. Every process writes its own node/topic table to
`%TEMP%\nav2_shim_graph\<pid>.graph`; readers merge all of them and drop entries whose owning pid
has exited. That is how `ros2 topic list` works on Windows without DDS. Remote topics appear with
the source `remote:<host:port>`.

**The writer never loses an update.** Rewriting a graph file is an **in-place replace**
(`MoveFileEx(REPLACE_EXISTING)`), never delete-then-rename: that would leave a window in which the
name does not exist, and a reader landing in that window reads a running node as "gone" — silently.
A rejected swap is not the end either: it sets a `dirty_` flag and a retry thread writes a fresh
`.tmp` and swaps again.

## Shared-memory transport

`nav2/shm_transport` is the local bus:

| Property | Meaning |
| --- | --- |
| Channel name | `ros2shim` + topic name (optional `NAV2_SHIM_NAMESPACE` prefix) |
| Topology | **single writer + many readers**, each reader owns its cursor |
| Falling behind | The oldest messages are dropped (`dropped` counter) |
| Slot geometry | Fixed by `NAV2_SHM_*`; publisher and subscriber must agree (`bridge_env.bat` unifies it) |

Platform differences are confined to `ShmChannel::Impl`:

| What is synchronised | Windows | Linux / POSIX |
| --- | --- | --- |
| Channel body | `CreateFileMapping(Local\nav2shim_<name>_data)` | `shm_open` + `ftruncate` + `mmap` |
| Writer lock | Named mutex + 5 s bounded wait | In-segment `pthread_mutex_t` (`PROCESS_SHARED` + `ROBUST`) + `timedlock` |
| Waking readers | Named manual events | In-segment `pthread_cond_t` **broadcast** (`sem_post` wakes only one — not enough) |
| Segment lifetime | Kernel destroys it when the last handle closes | `flock` shared lock on the segment: the kernel reclaims it when a process dies, the last holder `shm_unlink`s |

Two hard-won rules (both have regression tests): **`EOWNERDEAD` must be treated as "the lock was
acquired"** (a `ROBUST` mutex hands the lock over when its owner dies, and only the caller of
`pthread_mutex_consistent()` may release it again); **segment lifetime must not rely on an
in-segment reference count** (a `kill -9`-ed process never decrements it, so the name is never
reclaimed).

## How actions are built on shared memory

Upstream an action sits on two topics (`_feedback`, `_status`) and two services (`_send_goal`,
`_cancel_goal`). With no DDS here, the same thing is spread over **6 shared-memory channels**:

| Channel | Writer | Readers | Payload |
| --- | --- | --- | --- |
| `<action>/_goal` | client | server | 8-byte LE goal id + goal body |
| `<action>/_goal_ack` | server | all clients | id + 1 byte (0 = REJECT, 1 = ACCEPT) |
| `<action>/_cancel` | client | server | id (no body) |
| `<action>/_cancel_ack` | server | all clients | id + 1 byte |
| `<action>/_feedback` | server | all clients | id + feedback body |
| `<action>/_result` | server | all clients | id + 1-byte `ResultCode` + result body |

The conventions that must hold (all written down after being learned the hard way):

- **An id replaces the UUID.** Upstream claims a goal with a 16-byte UUID; here a process-level
  `static` counter emits a `uint64_t`, and `make_uuid(id)` spreads its first 8 bytes into a UUID for
  the `rclcpp_action` API to look at. The counter is **process-wide**, not per client — otherwise
  the server cannot tell two messages apart.
- **Each client / server gets its own read cursor.** `Node::get_private_channel` `open()`s a fresh
  object every time, while `get_channel` hands the same object to everyone asking for that name —
  otherwise a client that talks to itself would swallow another client's ack.
- **One read cursor per topic even within a node.** Correct for subscriptions, but **two
  subscriptions to the same topic in one node steal from each other**. Upstream has exactly that
  shape (`enable_stamped_cmd_vel` subscribes to both `Twist` and `TwistStamped` on one topic), so it
  is pinned to `false`.
- **`seek_to_latest()` at construction.** A shared-memory segment is a kernel object: after the
  writer exits, the old messages are still there. Without the seek, a freshly started process answers
  the previous round's goal (which, not being active yet, gets a REJECT).
- **A result acked in the same `poll()` is delivered one beat later.** When the server lives in the
  same process this is almost always the case. Delivering it immediately makes a caller that sees
  "result before goal response" treat it as the previous goal's result — and that goal then never
  completes.
- **Observability**: `ROS2_SHIM_ACTION_TRACE=1` prints one line per send/receive on all six channels.

## The plugin registry — keys must be namespace-qualified

Upstream pluginlib reads a plugin XML where `name` is what the config looks up and `type` is merely
constructed. This shim does not read XML; registration and lookup both use the exported spelling.
So keys **must not be short names** — `dwb_critics::PathAlignCritic` and `mppi::critics::PathAlignCritic`
are two different classes. A short name lets them overwrite each other, and the overwritten one is
then used as a pointer to **another base class** (the `Base` of the `static_cast<Base *>` in the
factory is the base registered at load time) — the virtual call jumps into the wrong function, and
the symptom is a segfault rather than a readable error.

Lookup order: "namespace-qualified spelling → after `normalize()`", see `detail::lookup_keys()` /
`detail::find_factory()`, with cases in `tests/plugin_names.cpp`.

## Lifecycle and shutdown order

Shutdown order is not a detail here; it is the line between "exits gracefully" and "does not".

nav2 puts its cleanup into rcl's pre-shutdown callback instead of a destructor:
`nav2::LifecycleNode` registers one at construction that runs `runCleanups()`. Upstream can do that
because `rclcpp::shutdown()` closes the context, and closing the context fires those callbacks.
The shim therefore has to reproduce the same semantics:

- **One context per process** (`Context::get_global()`). Originally every `NodeBaseInterface` had
  its own private context, so `rclcpp::shutdown()` could not reach any of them — registering a
  callback was equivalent to registering nothing.
- **`remove_pre_shutdown_callback()` really removes.** Nodes unregister their callback on
  destruction; failing to remove means calling into a freed object at the next shutdown.
- **`rclcpp::shutdown()` runs the callbacks first, then clears the `ok` flag**, matching upstream.

Why it is mandatory: `Costmap2DROS` owns a `std::unique_ptr<std::thread>` map-update thread that is
joined only in `on_deactivate()`. If the node is still ACTIVE at shutdown, that member (a derived
class) is destroyed before the base destructor runs `runCleanups()` — and a joinable thread
destroyed without join calls `std::terminate`. That was the fate of `planner_server` /
`controller_server` once.

## Ubuntu side — simulator and relay

**Simulator** (`bridge/ubuntu/ros2_bridge_sim/`, pure Python): a 24 m × 24 m static map (0.05 m
grid), rectangular obstacles, a 360-ray lidar model, differential-drive integration. It publishes
`/odom` `/scan` `/tf` `/tf_static` `/map` `/clock` and subscribes `/cmd_vel`. **It has no dynamics
or sensor-noise model** — it is deterministic, same seed and same commands give the same run.

**Relay** (`bridge/ubuntu/ros2_link/relay.py`): the real-ROS2 agent and the simulator agent are
**two independent gRPC servers**; the protocol is identical but the graphs are not connected (the
simulator graph only ever lived in the Python process's `graph.Graph` and never entered DDS; the
real ROS2 graph is on DDS, where the simulator agent cannot see it). The relay is a gRPC client *and*
server at once, merging the two graphs into one for Windows — necessary because the daemon is a
**single-peer** model.

The relay also handles:

- **Same-named topics do not mix.** The winner keeps the original name; the loser is moved to
  `/real/<topic>` or `/sim/<topic>` (`--collision prefer-sim|prefer-real`). **No data is lost** —
  they are simply separated.
- **Downstream `/cmd_vel` is routed to the side that *consumes* it**, not the side that publishes
  it: on the real ROS2 side the velocity smoother publishes it, while the simulator only
  subscribes. Routing it the wrong way sends the command to the real robot and the simulated car
  never moves.
- **Action goal ids get a prefix** (`sim:` / `real:`), because two upstreams may issue the same uuid
  and the downstream "follow this goal" call must be able to tell them apart unambiguously.

## How the transcoding is proven correct

`bridge/tests/generate_fixtures.py` **parses 307 struct layouts out of the compatibility-layer
headers** (an implementation independent of the C++ one), then uses `rosbags` (ROS2 Jazzy's CDR
rules) to generate sample messages for 17 topics, writing both `*.cdr` and `*.shim`.
`bridge_cdr_check` asserts:

```
cdr_to_shim(cdr) == shim        shim_to_cdr(shim) == cdr        cdr -> shim -> cdr round trip
```

Result: 35 fixtures, 105 byte comparisons, 0 failures — the transcoding is byte-exact in both
directions.

## Performance baseline

Shared memory is **not zero-copy**. A publish performs 4 copies:

```
publish(msg) → serialize()      copy1 struct→bytes
             → memcpy into slot  copy2 bytes→slot
============== process boundary (sharing, not copying)==============
subscribe() → memcpy out of slot copy3 slot→reader buffer
            → deserialize()      copy4 bytes→struct
```

The accurate description is **zero-syscall, not zero-copy**. What is saved is the kernel socket/UDP
stack.

Measured (Release / 6 P-cores, full path including serialize/deserialize):

| Payload | Wire size | Same-process msg/s | Cross-process msg/s | Latency p50 |
| --- | --- | --- | --- | --- |
| Twist | 48 B | 1.32 M | 1.40 M | 0.5 µs |
| Odometry | 709 B | 462 k | 483 k | 1.6 µs |
| LaserScan | 5813 B | 87 k | 88 k | 8.8 µs |

**Cross-process ≈ same-process** — the best evidence that shared memory was never paying for the
process boundary.
