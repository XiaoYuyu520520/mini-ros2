---
layout: default
title: 1 快速开始 · 连接机器人
---

# 1.8 连接机器人

<div class="overview">
<div class="label">本页总览</div>
<p>本节说明跨机通信的配置项、启动顺序，以及连不上时按什么顺序查。</p>
<ul>
  <li><a href="#order">启动顺序</a></li>
  <li><a href="#addr">地址与端口在哪配</a></li>
  <li><a href="#check">连上之后看什么</a></li>
  <li><a href="#fail">连不上怎么查</a></li>
</ul>
</div>

<a id="order"></a>

## 1.8.1 启动顺序

<div class="callout warn">
<span class="label">重要</span>
<p><strong>先起 Ubuntu 侧，再起 Windows 侧。</strong>顺序反了 Windows 会扫不到对端 ——
这是「远端话题全缺」最常见的原因。</p>
</div>

| 顺序 | 在哪台机器 | 起什么 | 命令 |
| --- | --- | --- | --- |
| ① | Ubuntu | 仿真 agent（或真 ROS2 agent） | `bash bridge/scripts/ubuntu_sim_ctl.sh start` |
| ② | Ubuntu | 中继（合并两张图） | `bash start/connector.sh up -d` |
| ③ | Windows | 点源环境 | `. .\start\加载模拟ROS环境变量.ps1` |
| ④ | Windows | 验证 | `ros2 topic list -t` |

守护进程不用手工起：<code>ros2</code> 的第一条命令会自己拉起它并等第一次同步完成
（等的是就绪文件，不是固定 sleep）。冷启动实测 **1.3–2.8 s**。

<a id="addr"></a>

## 1.8.2 地址与端口在哪配

| 要改什么 | 在哪 | 怎么改 |
| --- | --- | --- |
| 守护进程连谁 | 环境变量 `ROS2_BRIDGE_AGENT` | 点源**之前**设 `$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'`；或 `ros2_bridge.bat start --agent host:port`。**不设就自动发现**（LAN 扫描） |
| agent 绑定哪个地址 | `gRPC/ros2_gRPC_sdk/config/ros2_gRPC_sdk.yaml` | `server.address: "0.0.0.0:50051"`、`discovery.ports: [50051]` |
| 中继的三个端口 | `ROS2_LINK_SIM_PORT` / `ROS2_LINK_REAL_PORT` / `ROS2_LINK_PORT` | 默认 50051 / 50111 / 50060 |
| 同名话题谁保住原名 | `ROS2_LINK_COLLISION` | `prefer-sim`（默认）或 `prefer-real` |
| SSH 目标机 | `bridge/scripts/ubuntu_ssh.py` 的 `DEFAULT_TARGET` | 命令行覆盖 `--target wang@IP` |

本机约定：设备 `wang@192.168.0.153`，agent 端口 50051，密钥
`C:\Users\Administrator\Desktop\app\ssh_key\id_ed25519`。

<a id="check"></a>

## 1.8.3 连上之后看什么

```powershell
ros2 topic list -t
```

正常会看到（只有守护进程 + Ubuntu 仿真时是 7 条）：

```
/clock [rosgraph_msgs/msg/Clock]
/cmd_vel [geometry_msgs/msg/Twist]
/map [nav_msgs/msg/OccupancyGrid]
/odom [nav_msgs/msg/Odometry]
/scan [sensor_msgs/msg/LaserScan]
/tf [tf2_msgs/msg/TFMessage]
/tf_static [tf2_msgs/msg/TFMessage]
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

<a id="fail"></a>

## 1.8.4 连不上怎么查

按这个顺序：

### ① Ubuntu 侧起来了吗

```bash
bash bridge/scripts/ubuntu_sim_ctl.sh status
bash start/connector.sh status
bash start/connector.sh check
```

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
ss -lnt | grep 50060
```

## 1.8.5 跨机还是回环

中继的设计让<strong>同一份配置在本机和跨机之间只差一个地址</strong>：

| | 上游连法 | 对外监听 | Windows 连 |
| --- | --- | --- | --- |
| 本机开发 | `127.0.0.1` | `0.0.0.0:50060` + `127.0.0.1:50060` | `127.0.0.1:50060` |
| 跨机 | `127.0.0.1`（上游都在本机） | `0.0.0.0:50060` | `<ubuntu-ip>:50060` |

```powershell
# 跨机只改这一行
$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'
```

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="linux">1.7 Ubuntu 上手</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="../System_configuration/environment">2.1 环境变量配置</a></div>
</div>
