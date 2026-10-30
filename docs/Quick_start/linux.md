---
layout: default
title: 1 快速开始 · Ubuntu 上手
---

# 1.7 Ubuntu 上手

<div class="overview">
<div class="label">本页总览</div>
<p>Ubuntu 这一侧要起两个东西：<strong>一个 gRPC agent</strong>（把话题发出去），
以及<strong>中继</strong>（把「真 ROS2 图」和「仿真图」合并成一张给 Windows）。</p>
<ul>
  <li><a href="#sim">起仿真小车</a></li>
  <li><a href="#real">用真装的 ROS2 当对端</a></li>
  <li><a href="#relay">起中继（合并两张图）</a></li>
  <li><a href="#env">加载环境变量</a></li>
</ul>
</div>

## 1.7.1 准备工作

```bash
cd ~/code/CPU
```

如果这台机器还没装依赖，先装：

```bash
sudo apt-get install -y build-essential cmake ninja-build zlib1g-dev \
  libeigen3-dev libboost-dev libboost-serialization-dev libboost-thread-dev \
  libboost-system-dev libboost-atomic-dev libboost-chrono-dev libboost-date-time-dev \
  libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc libyaml-cpp-dev
```

Python 虚拟环境（Ubuntu 侧叫 <code>.venv</code>，和 Windows 侧的 <code>E:\venvs\py312</code> 不是同一个）：

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r bridge/ubuntu/ros2_bridge_sim/requirements.txt
```

<a id="sim"></a>

## 1.7.2 起仿真小车

本仓库自带一个纯 Python 的仿真小车（24 m × 24 m 静态地图 + 360 线雷达 + 差速运动学），
扮演真实机器人。启动脚本在 tmux 会话 <code>sim</code> 里跑它：

```bash
bash bridge/scripts/ubuntu_sim_ctl.sh start
bash bridge/scripts/ubuntu_sim_ctl.sh status
```

| 子命令 | 作用 |
| --- | --- |
| `start` | 起仿真 + agent，默认监听 `0.0.0.0:50051` |
| `status` | pid + 端口 |
| `log 40` | 尾随 <code>~/sim.log</code> |
| `stop` | 停 |

<div class="callout note">
<span class="label">提示</span>
<p><code>start</code> 认 <code>--port N</code>、<code>--goal X,Y</code>，
不认识的参数原样转给 <code>run_sim</code>。</p>
</div>

想交互看世界就直接跑：

```bash
cd bridge/ubuntu
PYTHONPATH=. ../.venv/bin/python -m ros2_bridge_sim.run_sim --seed 7 --goal 6,-4
```

仿真会发布 `/odom` `/scan` `/tf` `/tf_static` `/map` `/clock`，订阅 `/cmd_vel`。
它<strong>没有动力学与传感器噪声模型</strong> —— 确定性，同种子同命令可复现。

<a id="real"></a>

## 1.7.3 用真装的 ROS2 当对端

Ubuntu 上如果已经装了 ROS2 Jazzy，可以让<strong>真的 ROS2 图</strong>当对端：

```bash
# 一次性：装 ROS2 jazzy
bash bridge/scripts/ubuntu_ros2_setup.sh

# 起真 ROS2 图 + agent（--backend rclcpp）
bash bridge/scripts/ubuntu_ros2_peer.sh start
bash bridge/scripts/ubuntu_ros2_peer.sh status
```

它起的是 `talker`（`/chatter`）、`add_two_ints_server`、`ros2_peer_service.py`
（`/probe_trigger`）、`fibonacci_action_server`，再加一个用真 rclcpp 后端的
`ros2_grpc_sdk_agent` —— 那个 agent 才是 Windows 守护进程连的对象。

<div class="callout note">
<span class="label">提示</span>
<p>真 ROS2 agent 监听 <code>50111</code>，仿真 agent 监听 <code>50051</code>。
两个都起的话，用下面的中继把它们合并。</p>
</div>

<a id="relay"></a>

## 1.7.4 起中继（合并两张图）

<div class="callout note">
<span class="label">为什么需要中继</span>
<p>真 ROS2 agent 和仿真 agent 是<strong>两个各自独立的 gRPC 服务端</strong>，协议一样但图不通：
仿真图只活在 Python 进程的 <code>graph.Graph</code> 里，从没进过 DDS；真 ROS2 图在 DDS 上，
仿真 agent 看不见它。</p>
<p>而 Windows 侧的守护进程是<strong>单 peer 模型</strong>（只连一个 agent），所以必须有人把两张图
合并成一张。中继同时扮演 gRPC 客户端和服务端，Windows 侧<strong>一行都不用改</strong>。</p>
</div>

```bash
bash start/connector.sh up -d      # 后台起
bash start/connector.sh status     # 看状态
bash start/connector.sh check      # 只验证两端 agent 是否活着
bash start/connector.sh down       # 停
```

端口约定（都可用环境变量改）：

| 角色 | 端口 | 环境变量 |
| --- | --- | --- |
| 仿真 agent | 50051 | `ROS2_LINK_SIM_PORT` |
| 真 ROS2 agent | 50111 | `ROS2_LINK_REAL_PORT` |
| 中继对外（给 Windows） | 50060 | `ROS2_LINK_PORT` |

### 同名话题怎么办

那台机器上真 ROS2 和仿真**都发** `/scan` `/odom` `/tf` `/clock`。中继会让赢家保住原名，
输家改挂 `/real/<话题>` 或 `/sim/<话题>`，**不丢数据**，只是不让两台机器的数据绞在一起：

```bash
export ROS2_LINK_COLLISION=prefer-sim    # 默认：仿真保住 /scan，真 ROS2 的变 /real/scan
export ROS2_LINK_COLLISION=prefer-real   # 反过来
```

<a id="env"></a>

## 1.7.5 加载环境变量

```bash
source ~/code/CPU/bridge/linux/bridge_env.sh
ros2 topic list -t
```

不想改当前 shell 就一条命令了事：

```bash
bash ~/code/CPU/bridge/linux/ros2.sh topic list -t
```

Ubuntu 侧的守护进程用四个脚本（和 Windows 的四个入口一一对应）：

```bash
bash bridge/linux/ros2_bridge.sh start     # 后台起（走 setsid，活得比调用它的 shell 久）
bash bridge/linux/ros2_bridge.sh status
bash bridge/linux/ros2_bridge.sh pid
bash bridge/linux/ros2_bridge.sh stop
bash bridge/linux/ros2_bridge.sh log 40
```

<div class="callout note">
<span class="label">提示</span>
<p>Ubuntu 上也要守护进程：仿真的话题只活在它自己的进程里，对外的唯一出口是 agent；
守护进程把 agent 的话题镜像进本机的共享内存图，nav2 才看得见 <code>/scan</code>、
<code>/odom</code>、<code>/map</code>。<code>/cmd_vel</code> 反过来一样。</p>
</div>

## 1.7.6 在 Ubuntu 上跑 nav2

同一份脚本，<code>python</code> 写成 <code>python3</code>、反斜杠换 <code>/</code>：

```bash
cd ~/code/CPU
export ROS2_BRIDGE_AGENT=127.0.0.1:50060     # 仿真就在本机；不设就靠 LAN 自动发现

bash bridge/scripts/ubuntu_sim_ctl.sh start
bash bridge/linux/ros2.sh service call /sim/reset std_srvs/srv/Trigger   # 起栈前复位

python3 bridge/scripts/run_nav2_stack.py --goal auto --report /tmp/goal.json
python3 bridge/scripts/run_slam_demo.py --duration 60
python3 bridge/scripts/run_nav2_stack.py stop
```

实测（2026-09-18，设备 8 核）：栈 25 s 报到 active，<code>--goal auto</code> **SUCCEEDED**
（<code>/cmd_vel</code> 119 条、<code>/plan</code> 6 条、从 (0,0) 走到 (2.35, 0.03)，2.35 m）。

## 1.7.7 从 Windows 操作这台机器

```bash
python bridge/scripts/ubuntu_ssh.py "tail -n 20 ~/sim.log"                             # 跑任意 shell
python bridge/scripts/ubuntu_ssh.py --file bridge/scripts/ubuntu_sim_ctl.sh -- start   # 推脚本并执行
python bridge/scripts/sync_to_ubuntu.py --run                                          # 同步仓库切片并启动
```

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="windows">1.6 Windows 上手</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="connect">1.8 连接机器人</a></div>
</div>
