---
layout: default
title: 1 快速开始 · 连接机器人
---

# 1.4 连接机器人

<div class="overview">
<div class="label">本页总览</div>
<p>目标：让 Windows 上的 ros2 / nav2 / viz 同时看到 <strong>Ubuntu 上真实的 ROS2</strong> 和
<strong>Ubuntu 上的模拟 ROS2</strong>，并且能往两边发指令。</p>
<ul>
  <li><a href="#topo">目标与拓扑</a></li>
  <li><a href="#order">启动顺序</a></li>
  <li><a href="#relay">为什么需要接线器</a></li>
  <li><a href="#addr">地址与端口在哪配</a></li>
  <li><a href="#check">连上之后看什么</a></li>
  <li><a href="#fail">连不上怎么查</a></li>
  <li><a href="#loop">跨机还是回环</a></li>
</ul>
</div>

<a id="topo"></a>

## 1.4.1 目标与拓扑

Ubuntu 上其实有**两张互相看不见的 ROS2 图**：

* **真 ROS2**：机器人 / Gazebo / nav2 真栈，话题在 DDS 上；
* **模拟 ROS2**：仓库自带的 `ros2_bridge_sim`（Python 假车 + 雷达），话题只活在它自己的进程里。

前者在 DDS 上，后者不在，所以它们**不会自动互通**。接线器（`ros2_link.relay`）把两张图合成
一张，再对 Windows 开一个 gRPC 端口；Windows 的守护进程只连合成后的那一个端口。

```text
                        Ubuntu
  ┌───────────────────────────────────────────────────────┐
  │  真 ROS2 图（DDS）              模拟 ROS2 图（Python）  │
  │  /scan /odom /tf ...            /scan /odom /tf ...    │
  │        │                              │                │
  │  agent :50111                   agent :50051           │
  │  (--backend rclcpp)             (ros2_bridge_sim)      │
  │        └──────────┬───────────────────┘                │
  │             接线器 relay :50060                        │
  │          （合并两张图，重名话题自动改名）                │
  └──────────────────────┬────────────────────────────────┘
                         │ gRPC
                 ┌───────┴────────┐
                 │ Windows 守护进程 │ → 共享内存 → ros2 / nav2 / viz
                 └────────────────┘
```

三个端口各是什么：

| 端口 | 谁在听 | 给谁连 |
| --- | --- | --- |
| `50111` | 真 ROS2 的 agent | 接线器（本机） |
| `50051` | 仿真的 agent | 接线器（本机） |
| `50060` | 接线器 | **Windows 守护进程** |

<a id="order"></a>

## 1.4.2 启动顺序

<div class="callout warn">
<span class="label">重要</span>
<p><strong>先起 Ubuntu 侧（含接线器），最后起 Windows。</strong>顺序反了 Windows 会扫不到对端 ——
这是「远端话题全缺」最常见的原因。</p>
</div>

| 顺序 | 在哪台机器 | 起什么 | 命令（都在仓库根目录） |
| --- | --- | --- | --- |
| ① | Ubuntu | 真 ROS2 的 gRPC 面（不接真机器人可跳过） | `bash bridge/scripts/ubuntu_ros2_peer.sh start agent` |
| ② | Ubuntu | 模拟机器人 | `bash bridge/scripts/ubuntu_sim_ctl.sh start` |
| ③ | Ubuntu | 接线器（合并 ① ②） | `bash start/connector.sh up -d` |
| ④ | Ubuntu | 先自检合并结果 | `bash bridge/linux/ros2.sh topic list -t` |
| ⑤ | Windows | 点源环境 + 连 50060 | 见下 |

### ① 真 ROS2 的 agent（只在要接真机器人 / 真 nav2 时需要）

```bash
. /opt/ros/jazzy/setup.bash
cd <repo>
bash bridge/scripts/ubuntu_ros2_peer.sh start agent
```

它默认在 `0.0.0.0:50111` 起 `ros2_grpc_sdk_agent --backend rclcpp`，把**当下这台机器上的
ROS2 图**开一个 gRPC 面。真 ROS2 栈本来就跑着的话，这一步不用重启它，只是多一个 agent。
agent 二进制要用 ROS2 支持编：

```bash
python3 gRPC/ros2_gRPC_sdk/scripts/build.py --with-ros2
```

`ubuntu_ros2_peer.sh start` 后面还可以跟话题/服务/动作的名字（`talker`、`service`、`trigger`、
`action`），做联调时一起起几个演示节点。

### ② 模拟机器人

```bash
cd <repo>
bash bridge/scripts/ubuntu_sim_ctl.sh start          # 默认端口 50051，日志 ~/sim.log
```

### ③ 接线器

```bash
cd <repo>
bash start/connector.sh up -d
```

它连本机的 `127.0.0.1:50051`（仿真）和 `127.0.0.1:50111`（真 ROS2），对外监听
`0.0.0.0:50060`。**只跑仿真、暂时不接真 ROS2 也能起**：缺的那一端会一直重试，后起来能自动接上。

### ④ 先在 Ubuntu 上确认

```bash
bash start/connector.sh status                  # 三个端口：sim / real / link
bash start/connector.sh check                   # 两端 agent + relay 是否都应答
bash bridge/linux/ros2.sh topic list -t         # 从 Linux 侧看合并后的图
```

<div class="callout note">
<span class="label">提示</span>
<p>只跑仿真时 <code>check</code> 会报 <code>real agent ... DOWN</code>，那是预期的 —— 看
<code>sim</code> 和 <code>relay</code> 两行 UP、Windows 能列出仿真话题即可。</p>
</div>

### ⑤ Windows

```powershell
$env:ROS2_BRIDGE_AGENT = '<ubuntu-ip>:50060'    # 接线器的地址，不是 50051/50111
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

守护进程不用手工起：<code>ros2</code> 的第一条命令会自己拉起它并等第一次同步完成
（等的是就绪文件，不是固定 sleep）。冷启动实测 **1.3–2.8 s**。

<a id="relay"></a>

## 1.4.3 为什么需要接线器（以及同名话题怎么办）

* **两个 agent 不共享图。** 真 ROS2 agent 和仿真 agent 说得是同一种协议，但各自维护自己的
  图；接线器同时订阅两边、向下游（Windows）发布，图就合并了。
* **同名话题不混在一起。** `/scan`、`/odom`、`/tf`、`/clock` 两边都会有。默认
  `ROS2_LINK_COLLISION=prefer-sim`：**仿真的保住原名**（`/scan`），真 ROS2 的挂到
  `/real/scan`；改成 `prefer-real` 则反过来（仿真挂 `/sim/scan`）。两种都不丢数据。
* **服务和动作也合并。** 调用/目标会路由到真正拥有它的那一端。
* **Windows 侧只看到一个 agent。** 守护进程是单 peer 模型，不用为「两张图」改任何配置。

<a id="addr"></a>

## 1.4.4 地址与端口在哪配

| 要改什么 | 在哪 | 怎么改 |
| --- | --- | --- |
| 守护进程连谁 | 环境变量 `ROS2_BRIDGE_AGENT` | 点源**之前**设 `$env:ROS2_BRIDGE_AGENT = '192.168.1.50:50060'`；或 `ros2_bridge.bat start --agent host:port`。不设时点源脚本会填 `127.0.0.1:50060`（本机中继）。**自动发现不可靠**：LAN 扫描只扫 `50051`（仿真 agent 的口），而接线器对外是 `50060`，跨机时几乎一定扫不到，请显式给地址 |
| 真 ROS2 agent 的端口 | `ROS2_PEER_PORT`（`ubuntu_ros2_peer.sh`） | 默认 50111 |
| 仿真 agent 的端口 | `ubuntu_sim_ctl.sh start --port N` | 默认 50051 |
| 接线器的三个口 | `ROS2_LINK_SIM_PORT` / `ROS2_LINK_REAL_PORT` / `ROS2_LINK_PORT` | 默认 50051 / 50111 / 50060 |
| 同名话题谁保住原名 | `ROS2_LINK_COLLISION` | `prefer-sim`（默认）或 `prefer-real` |
| agent 绑定地址 | `gRPC/ros2_gRPC_sdk/config/ros2_gRPC_sdk.yaml` | `server.address: "0.0.0.0:50051"`、`discovery.ports: [50051]` |
| SSH 目标机 | `bridge/scripts/ubuntu_ssh.py` 的 `DEFAULT_TARGET` | 命令行覆盖 `--target user@IP` |

<a id="check"></a>

## 1.4.5 连上之后看什么

```powershell
ros2 topic list -t
```

只起仿真（没接真 ROS2）时是 7 条远端的 `/clock` `/cmd_vel` `/map` `/odom` `/scan` `/tf` `/tf_static`
（按字母序），另外 Windows 侧自己的节点（守护进程、CLI）各发一条本机的 `/rosout`
（`rcl_interfaces/msg/Log`），所以列表里通常还能看到它：

```
/clock [rosgraph_msgs/msg/Clock]
/cmd_vel [geometry_msgs/msg/Twist]
/map [nav_msgs/msg/OccupancyGrid]
/odom [nav_msgs/msg/Odometry]
/rosout [rcl_interfaces/msg/Log]
/scan [sensor_msgs/msg/LaserScan]
/tf [tf2_msgs/msg/TFMessage]
/tf_static [tf2_msgs/msg/TFMessage]
```

两张图都在时，同名话题会按 `prefer-sim` 分成两组：

```
/scan        /odom        /tf        /clock          ← 仿真（保住原名）
/real/scan   /real/odom   /real/tf   /real/clock     ← 真 ROS2
```

<div class="callout note">
<span class="label">提示</span>
<p>列表里有什么取决于<strong>当下谁在跑</strong>，这一点跟 ROS2 一致。整套 nav2 栈活着的时候
同一个命令能列出 <strong>64 条</strong>（<code>/global_costmap/*</code>、
<code>/controller_server/*</code> 都在里面）。</p>
</div>

再看三样：

```powershell
ros2 topic info /scan      # Source: remote (bridged over gRPC)
ros2 topic hz /scan        # average 9.446 Hz over 55 samples
ros2 bridge status         # 7 topics (7 remote, 0 local), known types: 40
```

只想确认「Windows 能不能往真 ROS2 发」：

```powershell
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.2}}" --once
```

再回 Ubuntu 上 `ros2 topic echo /cmd_vel --once`，能收到就说明合并图的发布方向也通了。

<a id="fail"></a>

## 1.4.6 连不上怎么查

按这个顺序：

### ① Ubuntu 侧起来了吗

```bash
bash bridge/scripts/ubuntu_sim_ctl.sh status
bash bridge/scripts/ubuntu_ros2_peer.sh status      # 接真 ROS2 时才有意义
bash start/connector.sh status
bash start/connector.sh check
```

`status` 里哪个端口是 `-`，就是那一端没起；`check` 会分别报告 sim / real / relay。

### ② 发现不看本机

守护进程和 agent 在同一台机器上时**必须给地址**：

```powershell
$env:ROS2_BRIDGE_AGENT = '127.0.0.1:50060'
```

### ③ 等的是就绪文件

冷启动 1.3–2.8 s。超预算还没好，去看守护进程日志：

```powershell
bridge\windows\ros2_bridge.bat log 40
```

### ④ 冷启动首扫偶发扫不到

有一次 <code>topic_list</code> 5.8 s 后报「远端话题全缺」，而同一轮的其它检查全过，
紧接着重跑就正常 —— 看着像首扫与对端 agent 的一次竞态，未定位。<strong>重跑一次</strong>。

### ⑤ 端口通不通

```bash
# 在 Ubuntu 上
ss -lnt | grep -E ':(50051|50111|50060) '
```

Windows 上：

```powershell
Test-NetConnection <ubuntu-ip> -Port 50060
```

<a id="loop"></a>

## 1.4.7 跨机还是回环

中继的设计让<strong>同一份配置在本机和跨机之间只差一个地址</strong>：

| | 上游连法 | 对外监听 | Windows 连 |
| --- | --- | --- | --- |
| 本机开发 | `127.0.0.1` | `0.0.0.0:50060` + `127.0.0.1:50060` | `127.0.0.1:50060` |
| 跨机 | `127.0.0.1`（上游都在本机） | `0.0.0.0:50060` | `<ubuntu-ip>:50060` |

```powershell
# 跨机只改这一行
$env:ROS2_BRIDGE_AGENT = '192.168.1.50:50060'
```

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="linux">1.3 Ubuntu 上手</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="../System_configuration/environment">2.1 环境变量配置</a></div>
</div>