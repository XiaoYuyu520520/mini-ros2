---
layout: default
title: ros2 命令行 ros2 CLI
---

# ros2 命令行 ros2 CLI

> **[中文](#中文) · [English](#english)**

---

# 中文

Windows 与 Ubuntu 上的 `ros2` 是**同一套子命令、同一套参数**。一次调用会把守护进程准备好，
之后复用；命令的语义与标准 ROS2 一致。

- Windows：`bridge\windows\ros2.bat ...`，或点源 `ros2.ps1` 之后直接 `ros2 ...`
- Ubuntu：`bridge/linux/ros2.sh ...`，或 `source bridge/linux/bridge_env.sh` 之后直接 `ros2 ...`

## 话题 topic

```bat
ros2 topic list -t                     :: 话题 + 类型
ros2 topic info /scan                  :: 发布者/订阅者/来源（remote 还是 local）
ros2 topic echo /odom --count 3        :: 打印消息内容
ros2 topic hz /scan                    :: 实时频率（Ctrl+C 结束）
ros2 topic type /scan
ros2 topic find sensor_msgs/msg/LaserScan
```

发布：

```bat
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.3 angular.z=0.1 --once
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.3, z: 0.1}}" -1
ros2 topic pub /scan sensor_msgs/msg/LaserScan "{ranges: [1.0, 2.0, 3.0]}" -1 -p   :: -p 先把要发的消息打出来
```

**取值两种写法都收**：真 ROS2 的那份 YAML（`"{linear: {x: 0.5}}"`、`"{data: Hello World}"`、
`"{ranges: [1.0, 2.0]}"`，以及块式缩进；引号是给 shell 的别丢），和本项目一直在用的扁平写法
（`linear.x=0.5 data=hi`、`linear={x: 0.5}`、`ranges=[1.0, 2.0]`）。两者落到同一组字段赋值上，
**装不下的值当场报错、不会静默发一条别的消息**。

短选项：`topic pub` 的 `-1` = `--once`、`-r` = `--rate`、`-t` = `--times`、`-p` = `--print`。

> **先订阅、还没有发布者**：`ros2 topic echo` / `hz` 会**等着**，发布者一出现就开始测 ——
> 这与标准 ROS2 一致，不会立刻退出。

## 服务 service

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

`service call` 只认**编解码表里**的类型；表外的类型跨不了机（本机 shm 上没这个限制）。

## 动作 action

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

短选项：`action send_goal` 的 `-t` = `--timeout`。

> **带数组字段的 goal**（`poses` / `viapoints` / `path`：`SmoothPath` / `FollowWaypoints` /
> `NavigateThroughPoses`）跨机 `build_goal` 会明确拒绝，发不出去；**本机 shm 上已经端到端跑通**。

## 参数 param

参数服务是**本机**的，不经过桥：

```bat
ros2 param list /controller_server -t     :: 参数 + 类型
ros2 param get /controller_server controller_frequency
ros2 param set /controller_server min_x_velocity_threshold 0.5
ros2 param describe /controller_server controller_frequency
ros2 param dump /bt_navigator             :: 导成能直接喂回 --params-file 的 YAML
```

`ros2 param list` 的节点名与上游一样**可以省略** —— 不写就遍历图里每个节点、逐段打印，
没有参数服务的节点跳过。`set` 会走 nav2 自己的校验回调，**非法值被拒且节点日志留痕**。

## 节点 / 接口 / 桥状态

```bat
ros2 node list
ros2 interface list
ros2 bridge status                     :: 桥接状态：远端、镜像与上行统计
```

## 运行期换控制器

```bat
ros2 topic pub /controller_selector std_msgs/msg/String data=MPPIFollowPath --once
```

取值：`FollowPath`（RPP，默认）/ `DWBFollowPath` / `MPPIFollowPath` / `GracefulFollowPath`。
栈不必重启。

## 地图

```bat
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt pgm
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt png   :: scale 模式要 png 才带透明通道
nav2\ros2_shim\build\map_server.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml ^
        -p yaml_filename:=C:\maps\office.yaml
```

## 常用组合

```bat
ros2 topic list -t
ros2 topic echo /scan --count 3
ros2 bridge status
ros2 service call /sim/reset std_srvs/srv/Trigger
nav2_stack.bat --goal auto
nav2_stack.bat stop
```

## 尚未提供

没有 `ros2 bag`、`ros2 run`、`ros2 launch`、`ros2 lifecycle`、`node info`、`interface show`、
`topic bw` / `topic delay`。`launch` 那一套语法本兼容层没有，启动靠 `bridge\windows\*.bat` /
`bridge/scripts/run_*.py`。

---

# English

`ros2` on Windows and Ubuntu is the **same set of subcommands with the same arguments**. One call
prepares the daemon and later calls reuse it; the semantics match standard ROS2.

- Windows: `bridge\windows\ros2.bat ...`, or dot-source `ros2.ps1` and type `ros2 ...`
- Ubuntu: `bridge/linux/ros2.sh ...`, or `source bridge/linux/bridge_env.sh` and type `ros2 ...`

## Topics

```bat
ros2 topic list -t                     :: topics + types
ros2 topic info /scan                  :: publishers/subscribers/source (remote or local)
ros2 topic echo /odom --count 3        :: print messages
ros2 topic hz /scan                    :: live rate (Ctrl+C to stop)
ros2 topic type /scan
ros2 topic find sensor_msgs/msg/LaserScan
```

Publishing:

```bat
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.3 angular.z=0.1 --once
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.3, z: 0.1}}" -1
ros2 topic pub /scan sensor_msgs/msg/LaserScan "{ranges: [1.0, 2.0, 3.0]}" -1 -p   :: -p prints first
```

**Two value syntaxes are accepted**: real-ROS2-style YAML (`"{linear: {x: 0.5}}"`,
`"{data: Hello World}"`, `"{ranges: [1.0, 2.0]}"`, and block indentation; the quotes are for the
shell — do not drop them) and the flat form this project has always used (`linear.x=0.5 data=hi`,
`linear={x: 0.5}`, `ranges=[1.0, 2.0]`). Both end up as the same set of field assignments, and
**a value that does not fit is rejected on the spot — it never silently publishes a different
message**.

Short options for `topic pub`: `-1` = `--once`, `-r` = `--rate`, `-t` = `--times`, `-p` = `--print`.

> **Subscribe first, no publisher yet**: `ros2 topic echo` / `hz` **wait**, and start measuring as
> soon as a publisher appears — matching standard ROS2; they do not exit immediately.

## Services

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

`service call` only knows types **in the codec table**; anything outside it cannot cross the
machine boundary (the local shm has no such limit).

## Actions

```bat
ros2 action list -t
ros2 action type /sim/drive
ros2 action find nav2_msgs/action/Spin
ros2 action send_goal /sim/drive ros2_bridge_sim/action/Drive distance=1.0
ros2 action send_goal /sim/spin nav2_msgs/action/Spin target_yaw=1.5 --timeout 25
ros2 action send_goal /sim/spin nav2_msgs/action/Spin "{target_yaw: 1.5}" -t 25
ros2 action send_goal /sim/back_up nav2_msgs/action/BackUp target.x=-0.4 speed=-0.2
ros2 action cancel /sim/drive 7         :: cancel a running goal (id comes from send_goal output)
```

Short option for `action send_goal`: `-t` = `--timeout`.

> **Goals with array fields** (`poses` / `viapoints` / `path`: `SmoothPath`, `FollowWaypoints`,
> `NavigateThroughPoses`) are explicitly rejected by the cross-machine `build_goal`; they cannot be
> sent over the bridge. **On the local shm they already run end to end.**

## Parameters

The parameter service is **local**; it does not go through the bridge:

```bat
ros2 param list /controller_server -t     :: parameters + types
ros2 param get /controller_server controller_frequency
ros2 param set /controller_server min_x_velocity_threshold 0.5
ros2 param describe /controller_server controller_frequency
ros2 param dump /bt_navigator             :: dump YAML that can be fed back to --params-file
```

`ros2 param list` accepts an omitted node name, just like upstream: with no name it walks every node
in the graph and prints section by section, skipping nodes without a parameter service. `set` goes
through nav2's own validation callback, so **invalid values are rejected and leave a line in the
node log**.

## Nodes / interfaces / bridge status

```bat
ros2 node list
ros2 interface list
ros2 bridge status                     :: bridge status: remote, mirroring and uplink counters
```

## Switching the controller at runtime

```bat
ros2 topic pub /controller_selector std_msgs/msg/String data=MPPIFollowPath --once
```

Values: `FollowPath` (RPP, default) / `DWBFollowPath` / `MPPIFollowPath` / `GracefulFollowPath`.
No stack restart needed.

## Maps

```bat
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt pgm
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt png   :: scale mode needs png for alpha
nav2\ros2_shim\build\map_server.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml ^
        -p yaml_filename:=C:\maps\office.yaml
```

## Common combinations

```bat
ros2 topic list -t
ros2 topic echo /scan --count 3
ros2 bridge status
ros2 service call /sim/reset std_srvs/srv/Trigger
nav2_stack.bat --goal auto
nav2_stack.bat stop
```

## Not provided

There is no `ros2 bag`, `ros2 run`, `ros2 launch`, `ros2 lifecycle`, `node info`, `interface show`,
`topic bw` / `topic delay`. The `launch` syntax is absent from this shim; startup uses
`bridge\windows\*.bat` / `bridge/scripts/run_*.py`.