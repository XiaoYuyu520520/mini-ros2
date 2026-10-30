---
layout: default
title: 3 基础应用开发 · ros2 命令行
---

# 3.1 ros2 命令行

<div class="overview">
<div class="label">本页总览</div>
<p>Windows 与 Ubuntu 上的 <code>ros2</code> 是<strong>同一套子命令、同一套参数</strong>。
一次调用会把守护进程准备好，之后复用；命令的语义与标准 ROS2 一致。</p>
<ul>
  <li><a href="#entry">怎么敲</a></li>
  <li><a href="#topic">话题 topic</a></li>
  <li><a href="#service">服务 service</a></li>
  <li><a href="#action">动作 action</a></li>
  <li><a href="#param">参数 param</a></li>
  <li><a href="#other">节点 / 接口 / 桥状态</a></li>
  <li><a href="#absent">没有提供的命令</a></li>
</ul>
</div>

## 3.1.1 怎么敲

### [Windows] 每个新窗口点源一次

```powershell
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

不想点源就一条命令了事（守护进程照样自动起）：

```powershell
bridge\windows\ros2.bat topic list -t
```

### [Linux]

```bash
source bridge/linux/bridge_env.sh
ros2 topic list -t
```

或者：

```bash
bash bridge/linux/ros2.sh topic list -t
```

## 3.1.2 话题 topic

```bat
ros2 topic list -t                     :: 话题 + 类型
ros2 topic info /scan                  :: 发布者/订阅者/来源（remote 还是 local）
ros2 topic echo /odom --count 3        :: 打印消息内容
ros2 topic hz /scan                    :: 实时频率（Ctrl+C 结束）
ros2 topic type /scan
ros2 topic find sensor_msgs/msg/LaserScan
```

### 发布

```bat
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.3 angular.z=0.1 --once
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.3, z: 0.1}}" -1
ros2 topic pub /scan sensor_msgs/msg/LaserScan "{ranges: [1.0, 2.0, 3.0]}" -1 -p   :: -p 先把要发的消息打出来
```

<div class="callout note">
<span class="label">提示</span>
<p><strong>取值两种写法都收</strong>：真 ROS2 的那份 YAML（<code>"{linear: {x: 0.5}}"</code>、
<code>"{data: Hello World}"</code>、<code>"{ranges: [1.0, 2.0]}"</code>，以及块式缩进；
引号是给 shell 的别丢），和本项目一直在用的扁平写法
（<code>linear.x=0.5 data=hi</code>、<code>linear={x: 0.5}</code>、<code>ranges=[1.0, 2.0]</code>）。
两者落到同一组字段赋值上，<strong>装不下的值当场报错、不会静默发一条别的消息</strong>。</p>
</div>

### 短选项

| 命令 | 选项 |
| --- | --- |
| `topic pub` | `-1` = `--once`、`-r` = `--rate`、`-t` = `--times`、`-p` = `--print` |
| `action send_goal` | `-t` = `--timeout` |

<div class="callout note">
<span class="label">提示</span>
<p><strong>先订阅、还没有发布者</strong>：<code>ros2 topic echo</code> / <code>hz</code> 会
<strong>等着</strong>，发布者一出现就开始测 —— 这与标准 ROS2 一致，不会立刻退出。</p>
</div>

## 3.1.3 服务 service

```bat
ros2 service list -t
ros2 service type /sim/reset
ros2 service find std_srvs/srv/Trigger
ros2 service call /sim/status std_srvs/srv/Trigger
ros2 service call /sim/set_paused std_srvs/srv/SetBool "{data: true}"
ros2 service call /sim/reset std_srvs/srv/Trigger
ros2 service call /sim/set_initial_pose nav2_msgs/srv/SetInitialPose ^
        pose.header.frame_id=map pose.pose.pose.position.x=2.0 pose.pose.pose.orientation.w=1.0
ros2 service call /sim/enable nav2_msgs/srv/Toggle enable=true
```

<div class="callout note">
<span class="label">注意</span>
<p><code>service call</code> 只认<strong>编解码表里</strong>的类型；表外的类型跨不了机
（本机 shm 上没这个限制）。</p>
</div>

## 3.1.4 动作 action

```bat
ros2 action list -t
ros2 action type /sim/drive
ros2 action find nav2_msgs/action/Spin
ros2 action send_goal /sim/drive ros2_bridge_sim/action/Drive distance=1.0
ros2 action send_goal /sim/spin nav2_msgs/action/Spin target_yaw=1.5 --timeout 25
ros2 action send_goal /sim/spin nav2_msgs/action/Spin "{target_yaw: 1.5}" -t 25
ros2 action send_goal /sim/back_up nav2_msgs/action/BackUp target.x=-0.4 speed=-0.2
ros2 action cancel /sim/drive 7         :: 取消一个还在跑的目标（id 见 send_goal 的输出）
```

<div class="callout note">
<span class="label">注意</span>
<p><strong>带数组字段的 goal</strong>（<code>poses</code> / <code>viapoints</code> / <code>path</code>：
<code>SmoothPath</code> / <code>FollowWaypoints</code> / <code>NavigateThroughPoses</code>）
跨机 <code>build_goal</code> 会明确拒绝，发不出去；<strong>本机 shm 上已经端到端跑通</strong>。</p>
</div>

## 3.1.5 参数 param

参数服务是**本机**的，不经过桥：

```bat
ros2 param list /controller_server -t     :: 参数 + 类型
ros2 param get /controller_server controller_frequency
ros2 param set /controller_server min_x_velocity_threshold 0.5
ros2 param describe /controller_server controller_frequency
ros2 param dump /bt_navigator             :: 导成能直接喂回 --params-file 的 YAML
```

<div class="callout note">
<span class="label">提示</span>
<p><code>ros2 param list</code> 的节点名与上游一样<strong>可以省略</strong> —— 不写就遍历图里每个节点、
逐段打印，没有参数服务的节点跳过。</p>
<p><code>set</code> 会走 nav2 自己的校验回调，<strong>非法值被拒且节点日志留痕</strong>。</p>
</div>

## 3.1.6 节点 / 接口 / 桥状态

```bat
ros2 node list
ros2 interface list
ros2 bridge status                     :: 桥接状态：远端、镜像与上行统计
```

## 3.1.7 运行期换控制器

```bat
ros2 topic pub /controller_selector std_msgs/msg/String data=MPPIFollowPath --once
```

取值：`FollowPath`（RPP，默认）/ `DWBFollowPath` / `MPPIFollowPath` / `GracefulFollowPath`。
栈不必重启。

## 3.1.8 没有提供的命令

没有 `ros2 bag`、`ros2 run`、`ros2 launch`、`ros2 lifecycle`、`node info`、
`interface show`、`topic bw` / `topic delay`。

`launch` 那一套语法本兼容层没有，启动靠 `bridge\windows\*.bat` /
`bridge/scripts/run_*.py`。

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="../System_configuration/shm">2.3 共享内存几何配置</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="nav2_stack">3.2 运行 nav2 导航栈</a></div>
</div>
