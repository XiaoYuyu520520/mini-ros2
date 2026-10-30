---
layout: default
title: 4 架构说明 · 总体架构
---

# 4.1 总体架构

<div class="overview">
<div class="label">本页总览</div>
<p>mini-ros2 由三个子项目拼成。理解它们的边界，是读懂整个仓库的前提。</p>
<ul>
  <li><a href="#tree">三个子项目</a></li>
  <li><a href="#flow">整体数据流</a></li>
  <li><a href="#grpc">第一层：gRPC 跨机传输</a></li>
  <li><a href="#daemon">第二层：桥守护进程</a></li>
  <li><a href="#shim">第三层：ROS2 兼容层</a></li>
  <li><a href="#discovery">发现机制：图注册表</a></li>
</ul>
</div>

<a id="tree"></a>

## 4.1.1 三个子项目

| 子项目 | 路径 | 角色 |
| --- | --- | --- |
| **胶水层** | `bridge/` | Windows 守护进程、`ros2` CLI、Ubuntu 仿真小车、跨机中继、入口脚本、验收文档 |
| **通信 SDK** | `gRPC/ros2_gRPC_sdk/` | gRPC 协议定义、客户端 / 服务端库、LAN 发现、CDR 编解码 |
| **兼容层 + 魔改 nav2** | `nav2/ros2_shim/` | 在 Windows 上造出 `rclcpp` / `rclcpp_action` / `tf2` 等接口，**原地编译上游 nav2 1.5.1** |

外加两块支撑：

| 子项目 | 路径 | 角色 |
| --- | --- | --- |
| 共享内存传输 | `nav2/shm_transport/` | 单写者多读者的环形缓冲 + 反射注册表 |
| slam_toolbox 移植 | `nav2/slam_toolbox_port/` | 上游 slam_toolbox 2.8.5 的 Windows 移植 |

<a id="flow"></a>

## 4.1.2 整体数据流

```
Ubuntu（真 ROS2 + 仿真）                          Windows
────────────────────────                          ──────
  真 ROS2 图 ──┐
               ├──gRPC──>  ros2_link 中继 ──gRPC──>  ros2_bridge_daemon.exe ──共享内存──> nav2 / ros2 CLI / 你的节点
  仿真图   ──┘             (relay.py)                  (单 peer 模型)                       (ros2_shim 兼容层)
                                                                                               │
  小车移动  <────────────────────gRPC───────────────────  <───────────────共享内存─────────────┘
```

<a id="grpc"></a>

## 4.1.3 第一层：gRPC 跨机传输

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

<div class="callout note">
<span class="label">关键设计</span>
<p><strong>载荷是不透明的 CDR 字节</strong>（含 4 字节封装头）+ 一个类型名 ——
传输层不需要认识消息结构，所以对端没实现的类型也能原样过桥。</p>
<p>服务端侧用可插拔的 <code>IRos2Backend</code>：<code>RclcppBackend</code>（真 ROS2 图）、
<code>MockBackend</code>（内存模拟，CI 用）。</p>
</div>

<a id="daemon"></a>

## 4.1.4 第二层：桥守护进程

`bridge/windows/src/bridge_daemon.cpp`，一个常驻进程，做四件事：

### ① 发现 + 持久连接

对本地各子网的 `--ports`（默认 50051）做一次 TCP 快速扫描，命中的端口用 `Ping` 握手确认
（回显 `ros_distro=sim` 与 agent 版本）。握手成功后建立一个 gRPC 通道（HTTP/2 + keepalive），
之后所有 `Subscribe` 流与 `Publish` 都复用它，不再重新发现。
实测 504 个地址的扫描 + 握手在 1 s 量级完成。

### ② 下行镜像（Ubuntu → Windows）

`ListTopics` 拿到远端话题表 → 按 `--mirror` 过滤 → 每个话题开一条 `Subscribe` 流 →
流上是原始 CDR → `ros2_bridge::cdr_to_shim()` 按类型转成兼容层结构体布局 →
写进共享内存通道 `ros2shim<topic>` → 同时
`ProcessRegistry::set_remote_topic(topic, type, "remote:<peer>")` 注册。

### ③ 上行转发（Windows → Ubuntu）

按 `--uplink`（默认 `/cmd_vel`、`/goal_pose`、`/initialpose`，加上 nav2 回话的 `/plan` 与
`/behavior_tree_log`）为每个话题起一个转发线程。

<div class="callout note">
<span class="label">关键设计</span>
<p><strong>由守护进程自己创建并持有</strong>那条共享内存通道。命名共享内存是
<code>Local\</code> 内核对象，只活在「有人持有句柄」期间；<code>ros2 topic pub --once</code>
自己建的通道会随进程退出消失，一次性发布就丢了。</p>
<p>转发线程<strong>订阅</strong>通道，<strong>本地没有发布者时也先向 agent 查询类型并预注册</strong>，
所以「nav2 之后才第一次发 <code>/cmd_vel</code>」也能工作。</p>
</div>

### ④ 维护图注册表

见下面「发现机制」。

<a id="shim"></a>

## 4.1.5 第三层：ROS2 兼容层

`include/` 里把 ROS2 的 C++ 接口造出来：

| 目录 | 内容 |
| --- | --- |
| `rclcpp/` | 36 个头：`node.hpp` / `publisher.hpp` / `subscription.hpp` / `parameter_service.hpp` / `clock.hpp` / `context.hpp` / `qos.hpp` … |
| `rclcpp_action/` `rclcpp_lifecycle/` | action 客户端 / 服务端；生命周期节点 |
| `tf2/` `tf2_ros/` `tf2_geometry_msgs/` `tf2_sensor_msgs/` | TF 库与消息 |
| `pluginlib/` | 插件注册表（**键必须带命名空间**） |
| `message_filters/` `bond/` `bondcpp/` `diagnostic_updater/` `interactive_markers/` | 上游依赖的替身 |
| `Poco/` `tbb/` `angles/` `uuid/` `Magick++/` | 第三方替身；`Magick++` 是 PGM + 8 位 PNG 图像编解码器 |

<div class="callout note">
<span class="label">关键设计</span>
<p>每个编译单元都被 <code>-include ros2_shim/prelude.hpp</code> 强制注入一层垫片 ——
这是「兼容层怎么骗过 nav2」的核心机制：<strong>上游 nav2 源码一行没改</strong>，缺什么就补什么。</p>
</div>

上游 nav2 1.5.1 的源码放在 `nav2/nav2_extracted/navigation2-1.5.1/`，由
`nav2/ros2_shim/CMakeLists.txt:96` 起的 `nav2_package_sources()` **原地 glob 编译**：
它 glob 每个包的 `src/**.cpp` 与 `plugins/**.cpp`，自动丢掉自带 `main()` 的文件
（那些位置交给 `apps/`），并为每个编译单元注入垫片。

<div class="callout note">
<span class="label">唯一一处上游改动</span>
<p><code>nav2_mppi_controller/tools/parameters_handler.hpp</code> 的两行
<code>value_to_string()</code>。这处改动由 <code>nav2/patches/</code> 里的 patch +
<code>verify_patch.py</code> 表达，构建期校验 —— 换上游版本时不会悄悄丢掉。</p>
</div>

<a id="discovery"></a>

## 4.1.6 发现机制：图注册表

没有 DDS，就没有 DDS 发现。每个进程在 `%TEMP%\nav2_shim_graph\<pid>.graph` 写一份自己的
节点 / 话题表；读者合并所有文件，并按 pid 判断注册者是否已退出（已退出的顺带清掉）。
这就是 Windows 上没有 DDS 却还能 `ros2 topic list` 的原因。远端话题以 `remote:<host:port>`
作为 source 出现。

<div class="callout note">
<span class="label">写的一侧不丢</span>
<p>重写一份图是<strong>就地替换</strong>（<code>MoveFileEx(REPLACE_EXISTING)</code>），
不先删旧文件 —— 先 <code>remove</code> 再 <code>rename</code> 会留出一个「名字不存在」的窗口，
落在窗口里的读者会把一个正在运行的节点读成「没有」，而且失败是安静的。</p>
<p>一次被拒绝的交换不是终点：交换失败时置一个 <code>dirty_</code> 标记，
交给重刷线程<strong>重新写一份 <code>.tmp</code> 再换</strong>。</p>
</div>

## 4.1.7 Ubuntu 侧：仿真与中继

**仿真**（`bridge/ubuntu/ros2_bridge_sim/`，纯 Python）：24 m × 24 m 静态地图（0.05 m 栅格）、
矩形障碍、360 线射线雷达、差速运动积分。发布 `/odom` `/scan` `/tf` `/tf_static` `/map` `/clock`，
订阅 `/cmd_vel`。**没有任何动力学与传感器噪声模型** —— 确定性，同种子同命令可复现。

**中继**（`bridge/ubuntu/ros2_link/relay.py`）：真 ROS2 agent 与仿真 agent 是**两个各自独立的
gRPC 服务端**，协议一样但图不通。中继同时扮演 gRPC 客户端与服务端，把两张图合并成一张暴露给
Windows —— 守护进程是**单 peer 模型**，所以这是必须的。

中继还负责三件事：

| 职责 | 做法 |
| --- | --- |
| **同名话题不混流** | 赢家保住原名，输家改挂 `/real/<topic>` 或 `/sim/<topic>`（`--collision prefer-sim\|prefer-real`），**不丢数据**，只是分开 |
| **`/cmd_vel` 路由给消费它的那一侧** | 真 ROS2 侧由 velocity smoother **发布**，仿真侧只有小车**订阅**。路由错了，命令会进真机器人，仿真车一步不动 |
| **action 的 goal_id 加前缀** | `sim:` / `real:`，否则两个上游各自发 uuid 可能撞，下游「follow 这个 goal」时无法无歧义地找回是哪个上游 |

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="../Basic_application/map">3.4 地图存取</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="shm_transport">4.2 共享内存传输</a></div>
</div>
