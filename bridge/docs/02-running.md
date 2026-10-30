# 02 · 运行手册

本文是操作手册：怎么构建、怎么起停、每个命令和参数是什么意思、出错先看哪里。
架构与设计取舍见 [`01-architecture.md`](01-architecture.md)，实测证据见
[`03-validation.md`](03-validation.md)。

## 0. 前置条件

| 位置 | 需要什么 | 检查 |
| --- | --- | --- |
| Windows | MSYS2 UCRT64（cmake / ninja / g++ / gRPC / protobuf），默认 `D:\WindowsServer2025_1\software\MSYS2`，**只有构建时需要** | `D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin\cmake.exe --version` |
| Windows | Python 3.12 虚拟环境 `E:\venvs\py312`（grpcio / protobuf / numpy / pytest / rosbags） | `E:\venvs\py312\Scripts\python.exe -c "import grpc,numpy"` |
| Ubuntu | Python 3.10+ 与一个虚拟环境；**不需要安装 ROS2** | `~/code/CPU/.venv/bin/python -V` |
| 网络 | Windows 能主动连到 Ubuntu 的 TCP 50051 | `Test-NetConnection 192.168.1.113 -Port 50051` |

MSYS2 装在别处时，所有脚本都接受 `--msys2 <根目录>`，或者先设置
`ROS2_BRIDGE_MSYS2` 再调用 `bridge_env.bat`。

**运行时不依赖 MSYS2**：移植过来的 nav2 / slam_toolbox / nav2_amcl 可执行文件是静态自包含的
（`objdump -p` 里只剩 `KERNEL32.dll` 和 `api-ms-win-crt-*`），`ros2.exe` 也是；守护进程因为
MSYS2 只提供 gRPC 的 DLL 而无法静态链接，构建时会把它依赖的 DLL 复制到 `bridge\windows\\build\`
（`bridge\windows\cmake\\bundle_runtime.cmake`，87 个、约 31 MB），Windows 加载器先看 exe 自己
所在目录，所以整条链路在没有装 MSYS2 的机器上也能跑。

## 1. 一次性准备

**Windows 侧构建**（守护进程 + `ros2` CLI + 转码自检）：

```bat
cd C:\Users\Administrator\Desktop\app\code\CPU
E:\venvs\py312\Scripts\python.exe bridge\scripts\build_bridge.py --test
```

产物：`bridge\windows\build\ros2_bridge_daemon.exe`、`ros2.exe`、`bridge_cdr_check.exe`。

**把 Ubuntu 侧推过去并启动仿真小车**：

```bat
E:\venvs\py312\Scripts\python.exe bridge\scripts\sync_to_ubuntu.py --run
```

`--run` 会在前台跑 `run_sim`；想让它常驻就用控制脚本：

```bat
E:\venvs\py312\Scripts\python.exe bridge\scripts\ubuntu_ssh.py --file bridge\scripts\ubuntu_sim_ctl.sh -- start
```

**nav2 demo** 的构建由 `run_nav2_demo.py` 自动完成（`--build-only` 只构建）：

```bat
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_nav2_demo.py --build-only
```

## 2. 入口一览：十一个 bat + 一个 PowerShell 脚本

| 脚本 | 用途 |
| --- | --- |
| `bridge\windows\bridge_env.bat` | 环境入口。被别的脚本 `call`，**只影响当前 cmd 窗口**（它故意不写 `setlocal`） |
| `bridge\windows\ros2.bat` | 在 Windows 上使用 ROS2：自动确保守护进程在跑**并完成第一次同步**，再把参数转给 `ros2.exe` |
| `bridge\windows\ros2.ps1` | 同一件事的 PowerShell 版本。**点源一次**，之后本窗口直接敲 `ros2 ...`；只导入环境变量，守护进程仍按需启动 |
| `bridge\windows\ros2_shell.bat` | 开一个新 PowerShell 窗口并在里面点源 `ros2.ps1`：环境变量和 `ros2` 函数都在。`.bat` 改不了调用方会话的环境，所以「bat 一键拿到完整环境」只有开新窗口这一条路 |
| `bridge\windows\ros2_bridge.bat` | 守护进程控制：`start` / `run` / `status` / `stop` |
| `bridge\windows\wait_bridge_ready.bat` | 启动守护进程并等它「首次连接尝试结束」；需要镜像话题的脚本（`ros2.bat` 与各 demo / 栈脚本，共七个）都 `call` 它。`ros2.ps1` 走的是同一条路，只是由 `ros2` 函数在第一条命令时代劳 |
| `bridge\windows\nav2_demo.bat` | 建图 + 规划 + 控制 的 demo（等价于 `run_nav2_demo.py`） |
| `bridge\windows\nav2_stack.bat` | 一键拉起**上游 nav2 服务器本体**：AMCL + `planner_server` + `controller_server` + `bt_navigator` + `behavior_server` + `smoother_server` + `waypoint_follower` + `lifecycle_manager`，可选直接下发目标；加 `--safety` 时把下面那两个安全节点**接进驱动链**；加 `--map FILE` 时用存好的图当 `/map`，而不是守着对端镜像进来的那张 |
| `bridge\windows\nav2_safety.bat` | 旁路的安全链：`velocity_smoother`（`/cmd_vel` → `/cmd_vel_smoothed`）+ `collision_monitor`（→ `/cmd_vel_monitored`），与正在跑的栈并行观察，不影响小车。进链的那一半见 `nav2_stack.bat --safety` |
| `bridge\windows\nav2_docking_route.bat` | 旁路的 `docking_server` + `route_server`：起两个节点并各推到 ACTIVE（`--duration` / `--params` / `stop`），日志在 `%TEMP%\nav2_docking_route\`。**只是「活着」**：没有 dock 实例、也没有图文件（证据 28） |
| `bridge\windows\slam_demo.bat` | 在 Windows 上跑 `slam_toolbox`，把桥里的 `/scan` + `/tf` 建成 `/slam_map` |
| `bridge\windows\amcl_demo.bat` | 在 Windows 上跑 `nav2_amcl`，用桥里的 `/scan` + `/map` 定位，发 `/amcl_pose` 与 `map -> odom` |
| `bridge\scripts\run_nav2_stack.py` | `nav2_stack.bat` 的**跨平台孪生**：Windows 与 Linux 跑同一份，参数一样（`--goal` / `--map` / `--safety` / `--duration` / `--params` / `stop`），日志同样一节点一份（Linux 在 `/tmp/nav2_stack/`） |
| `bridge\scripts\run_slam_demo.py` | `slam_demo.bat` 的**跨平台孪生**，同上 |
| `bridge\scripts\run_amcl_demo.py` | `amcl_demo.bat` 的**跨平台孪生**（`--initial-pose` 不给就从 `/odom` 起） |
| `bridge\scripts\run_nav2_safety.py` | `nav2_safety.bat` 的**跨平台孪生**（`--duration` / `--params` / `stop`） |
| `bridge\scripts\run_nav2_docking_route.py` | `nav2_docking_route.bat` 的**跨平台孪生**，同上 |

手工搭一个「ROS2 窗口」也很简单：

```bat
call bridge\windows\bridge_env.bat
ros2 topic list -t
ros2 topic echo /odom --count 1
```

PowerShell 里做同一件事要**点源** —— `.bat` 影响不了 PowerShell 会话本身（它跑在 `cmd` 子进程里，
环境跟着子进程一起走，所以直接跑 `ros2.bat` 只会打印用法）：

```powershell
. E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1
ros2 topic list -t
ros2 topic echo /odom --count 1
```

不想点源、想要一个现成窗口：`bridge\windows\ros2_shell.bat`（可以带一个起始目录）。它开一个新 PowerShell
窗口，并在里面点源 `ros2.ps1`。

关掉窗口，Windows 就恢复原样 —— 这套环境不写注册表、不改系统 PATH。

### Linux 侧：`bridge/linux` 的四个脚本

同一件事在 Linux 上是四个 `.sh`，和上面那四个 Windows 入口一对一（仓库里的 `.sh` 不带头执行位，
一律用 `bash` 调用）。**栈与建图不在这里**：它们不是再写一遍 shell，而是上面那两个 `.py`，两平台同一份
（平台差异收在 `bridge/scripts/_bridge_platform.py`），Ubuntu 上的完整跑法见 `BUILD.md` 6.3。

| 脚本 | 对应 | 用途 |
| --- | --- | --- |
| `bridge/linux/bridge_env.sh` | `bridge_env.bat` | **`source` 一次**：环境留在这个 shell 里，PATH 里还会有 `ros2` |
| `bridge/linux/ros2.sh` | `ros2.bat` | 一条命令用 ROS2：确保守护进程在跑、等首次同步、再把参数转给 CLI |
| `bridge/linux/ros2_bridge.sh` | `ros2_bridge.bat` | 守护进程控制：`start` / `run` / `status` / `pid` / `stop` / `log [N]` |
| `bridge/linux/wait_bridge_ready.sh` | `wait_bridge_ready.bat` | 起守护进程并轮询 `ROS2_BRIDGE_READY_FILE` |

```bash
bash bridge/linux/ros2.sh topic list -t      # 一次性用法，不碰当前 shell

source bridge/linux/bridge_env.sh            # 或者在本 shell 里常驻
ros2 topic list -t
```

两处和 Windows 不一样：

* 守护进程自己不 fork（`detach_process` 只做 `freopen` + `setvbuf`），所以 `start` 用 `setsid`
  给它一个自己的会话；pid 记在 `$ROS2_BRIDGE_BUILD/ros2_bridge_daemon.pid`，找不到时回退到按
  `/proc/<pid>/comm` 扫进程表——匹配的是**进程名**，不是命令行（`pgrep -f` 会把跑这个脚本的 ssh
  会话本身也匹配上）。
* **发现不看本机**：`discovery.cpp` 故意把本机所有地址（含 loopback）排除在扫描之外，所以守护进程
  和 agent 跑在同一台机器上时必须显式给地址，否则日志会一直
  `probing 253/253 hosts` + `retrying in 2000 ms`：

  ```bash
  ROS2_BRIDGE_AGENT=127.0.0.1:50051 bash bridge/linux/ros2_bridge.sh start
  ```

  跨机（Windows ↔ Ubuntu）不需要这一条。实测与证据见 `04-gaps.md` 第十四节。

## 3. 验收 1：在 Windows 上像 ROS2 一样用

```bat
cd C:\Users\Administrator\Desktop\app\code\CPU
bridge\windows\ros2.bat topic list -t
```

守护进程没在跑时，这条命令会自己完成「扫描局域网 → 握手 → 建立持久 gRPC 通道 → 把远端话题
镜像进共享内存」，实测冷启动 **3.1 s** 返回（见 03）：

```
/clock [rosgraph_msgs/msg/Clock]
/cmd_vel [geometry_msgs/msg/Twist]
/map [nav_msgs/msg/OccupancyGrid]
/odom [nav_msgs/msg/Odometry]
/scan [sensor_msgs/msg/LaserScan]
/tf [tf2_msgs/msg/TFMessage]
/tf_static [tf2_msgs/msg/TFMessage]
```

支持的子命令：

| 命令 | 说明 |
| --- | --- |
| `topic list [-t] [-v]` | 列话题；`-t` 带类型，`-v` 带来源 |
| `topic info <topic>` | 类型、发布/订阅计数、来源（`remote` 还是本机） |
| `topic type <topic>` | 只打印类型 |
| `topic echo <topic> [--count N]` | 打印消息内容；不支持 `--field`，字段用 `字段路径:` 全量打印 |
| `topic hz <topic> [--window N]` | 实时频率，**跑到 Ctrl+C 才停**（先排空环形缓冲再计时） |
| `topic pub <topic> <type> [values] [-1] [-r HZ] [-t N] [-p] [--duration S]` | 发布；取值两种写法都收（见下面「取值」一节），多余参数会直接报错退出 |
| `topic find <regex>` | 按正则过滤话题名 |
| `service list [-t] [-v]` / `type` / `find` | 服务：列、类型、过滤 |
| `param list <node> [-t]` / `get` / `set` / `describe` / `dump` | 参数：列（`-t` 带类型）、读、写、看描述符、导成能喂回 `--params-file` 的 YAML。调的是节点自己名字下那六个服务，**只在本机**，不经过 gRPC 桥（见证据 33） |
| `service call <service> <type> [values] [--timeout S]` | 调服务：请求走 `<name>/_request`，按自己的关联 id 在 `<name>/_response` 上等回答 |
| `action list [-t] [-v]` / `type` / `find` | 动作：列、类型、过滤（本地节点的 + 远端镜像进来的） |
| `action send_goal <action> <type> [values] [-t S] [-f]` | 下发目标，打印反馈与结果（`-t`=`--timeout`，`-f`=`--feedback`，反馈本来就一直打） |
| `action cancel <action> <goal id>` | 取消一个还在跑的目标（id 在 `send_goal` 的输出里） |
| `node list [-v]` | 列节点 |
| `interface list` | 列出桥接支持的消息类型（2026-09-19 起 **41** 种，证据 48 / 62 / 71） |
| `bridge status` | 桥接总览：远端、话题数、已知类型 |

`ros2.bat` 不带参数就打印上面这张表。动作那两条是**同一份协议**的客户端：目标写在
`<action>/_goal`（前面 8 字节 goal id），回答从 `<action>/_goal_ack` 回来，反馈和结果分别
走 `_feedback` 与 `_result`。本地 nav2 节点和远端机器人用的是同一套通道，所以「服务/动作是
本机节点回答的，还是桥转发过去的」对调用方没有区别。

### 取值：真 YAML 还是 `field.path=value`

`topic pub` / `service call` / `action send_goal` 的最后一个位置参数，在真 ROS2 里是**一段 YAML**
（`ros2 topic pub --help` 原文：*Values to fill the message with in YAML format (e.g. 'data: Hello World')*）；
本项目一开始只认扁平的 `field.path=value`。现在两种都收，而且**落到同一组字段赋值上**
（`{linear: {x: 0.5}}` 与 `linear.x=0.5` 是同一条）：

```bat
:: 真 ROS2 的 YAML 写法（引号是给 shell 的，别丢）
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.5}}" -1
ros2 topic pub /chatter std_msgs/msg/String "{data: Hello World}" -1
ros2 topic pub /scan sensor_msgs/msg/LaserScan "{ranges: [1.0, 2.0, 3.0]}" -1 -p
ros2 service call /sim/set_paused std_srvs/srv/SetBool "{data: true}"
ros2 action send_goal /sim/spin nav2_msgs/action/Spin "{target_yaw: 1.5}" -t 45

:: 本项目一直在用的扁平写法，两条都能用
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.5 data=hi --once
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "linear={x: 0.5}" -1

:: 消息数组：带下标的路径
ros2 action send_goal /navigate_through_poses nav2_msgs/action/NavigateThroughPoses poses.goals[0].pose.position.x=1.5 poses.goals[0].pose.orientation.w=1.0 -t 300

:: 消息数组：YAML 文档里的嵌套数组，和上面落到同一组赋值上
ros2 action send_goal /compute_path_through_poses nav2_msgs/action/ComputePathThroughPoses "{goals: {goals: [{pose: {position: {x: 1.5}}}, {pose: {position: {x: 1.33}}}]}}" -t 60

:: 块式缩进（从 --params-file 里把一段拄过来时最常见的形状）
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "linear:
  x: 0.5" -1
ros2 service call /sim/set_paused std_srvs/srv/SetBool "data: true"
```

- 嵌套映射按 `.` 摊平（`{pose: {position: {x: 1}}}` → `pose.position.x=1`），顶层键也可以直接写点号
  （`{linear.x: 0.5}`）；`[a, b]` 摊成编解码层要的逗号列表（`setSequence` 那份语法）。
- **带下标的路径能表达“消息数组”**：`poses.goals[0].pose.position.x=1.5`。YAML 文档那侧同样，
  `{goals: [{...}, {...}]}` 会按首元素判定这是“每条元素一个消息”再逐个摊平（混着标量和映射、
  或序列套序列会当场报错）。只给数组名不给下标（`poses=`）会直接提示该怎么写：
  `'poses' carries messages, one per element: write 'poses[0].' and then a field of that element`。
  `NavigateThroughPoses` / `ComputePathThroughPoses` / `FollowWaypoints` / `nav_msgs/Path` 的航点表
  就是这么发的（`03-validation.md` 证据 68）。
- 引号去掉一层：`data="hi"` 以前会把引号本身发出去（`echo` 打 `data: ""hi""`，退出码还是 0），现在发 `hi`。
  `null` / `~` / 空值 = 空字符串；`#` 之后到行尾是注释。
- 标量本身**不做类型翻译**：`{data: yes}` 在 `std_msgs/msg/String` 上仍然是 `yes`，在 bool 字段上按
  `true/false/yes/no/on/off`（不分大小写）读成布尔。
- **块式缩进也认**：`key:` 换行后再缩进写子键（`linear:` / `  x: 0.5`），同一层的键靠换行分隔；
  子块只要比父键缩进更深就行（缩进几个空格、用不用 tab 都不挑），`key:` 后面空着就是空值。
  块式里一个裸标量**到行尾结束** —— 这是 YAML 的规矩，也修掉了以前「`data: hi` 后面还有一行就被
  整段吞进同一个值」那种 rc=0 的静默错值。块式的**序列**（`ranges:` 换行再 `  - 1.0`）还不认，
  写成 `ranges: [1.0, 2.0]`。
- **装不下的值当场报错，不会静默发一条别的消息**：一个序列里既有标量又有映射、序列套序列、下标不是
  纯数字（`poses[i]`）、没配平的 `{` / `[`、`=` 前面缺字段名，全部退回非零并打印原因。字段名写错也一样
  （`no settable field 'nope'`）。
- 短选项：`-1` = `--once`、`-r` = `--rate`、`-t` = `--times`（`topic pub`）/`--timeout`（`action send_goal`）、
  `-p` = `--print`（先把要发的消息按 `topic echo` 的格式打一遍，写值时最有用）、`-f` = `--feedback`。
  不认识的选项现在直接报错 —— 以前会被当成话题名，然后在别的地方以别的理由失败。
- 真 CLI 有、这里**还没有**的：`-w/--wait-matching-subscriptions`、`--stdin`、`service call -r`、
  `topic echo --field` / `--qos-*`（见 `04-gaps.md` 第二十六节）。

shell 那一层：PowerShell 和 cmd 都会把 `"{linear: {x: 0.5}}"` 当成**一个**参数交过来；
没加引号时它可能被空格切成三个（`{linear:`、`{x:`、`0.5}}`），这种情况下 CLI 会按括号把后半截自动接回去。
值里想带真的引号，写 `data='"hi"'`。

### 调参：`ros2 param`

参数服务是**本机**的：`ros2 param` 不与 gRPC 桥打交道，它调的是节点自己名字下那六个服务
（`<node>/get_parameters`、`get_parameter_types`、`set_parameters`、`set_parameters_atomically`、
`describe_parameters`、`list_parameters`，见证据 33），所以对着**正在跑**的节点读和写都不用重启栈。

```bat
call bridge\windows\bridge_env.bat
ros2 param list                                        :: 不写节点：图里每个有参数的节点都列一遍
ros2 param list /controller_server -t                  :: 名字 + 类型
ros2 param get  /controller_server controller_frequency
ros2 param set  /controller_server min_x_velocity_threshold 0.5
ros2 param describe /controller_server controller_frequency
ros2 param dump /bt_navigator > bt.yaml                :: 能直接喂回 --params-file
```

几件值得记住的事：

- **`set` 会走节点自己的校验回调。** nav2 的 `ParameterHandler::validateParameterUpdatesCallback`
  拒绝的值，`set` 报失败、值不变，而且**节点日志里会留一行**：把 `min_x_velocity_threshold` 设成
  `-1.0` 之后，`%TEMP%\nav2_stack\controller_server.log` 里就有
  `The value of parameter 'min_x_velocity_threshold' is incorrectly set to -1.000000, it should be >=0.`。
  （那行的 `Setting parameter failed:` 后面是空的，因为 nav2 只置 `successful=false`、不填 `reason` ——
  上游 rclpy 对着同一个节点也是这句。）
- **声明过的参数才能写。** 服务调用永远不会「顺手建一个」参数：没声明的名字会被拒
  （`parameter 'x' has not been declared`），类型也不能就地改。
- **节点得在自己 spin。** `/bt_navigator_navigate_to_pose_rclcpp_node` 这类行为树客户端节点也有
  参数服务，但树不跑的时候它不被 spin，调用会 5 秒超时；先跑一个目标再调就正常（证据 33 里 12/12
  都答了）。对着一个根本不存在的服务，`ros2 param` 直接说 `unknown service`，不是干等超时。
- **`list` 的节点名可以省。** 与上游一样：不写就遍历图里每个节点，逐段打印 `<node>:` 加参数名，
  段与段之间空一行，`-t` 的时候每行再带上类型；完全没有参数服务的节点（比如
  `/ros2_bridge_daemon`）**直接跳过**，既不报错也不占时间 —— `paramCall` 先问图里的服务再调用，
  没有那个服务就谈不上 5 秒超时。指名的节点不存在时仍然报错（`unknown service`）。
- **参数服务不跨机；参数事件流已经进编解码表。** Ubuntu 上改不了 Windows 节点的参数
  （proto 里没有对应的 RPC）；`/parameter_events` 节点自己会发（证据 39），类型
  `rcl_interfaces/msg/ParameterEvent` 2026-09-17 起在桥的编解码表里（证据 48），所以这条流
  理论上能过桥 —— 跨机端到端**还没实测**（见 `04-gaps.md`）。

想看「谁在什么时候把哪个参数改成了什么」的话，话题比服务更直接 —— 它不需要事先知道
任何一个服务名，另一个进程订阅就行。`node.hpp` 里声明进 `new_parameters`、应用成功的 `set`
进 `changed_parameters`，`deleted_parameters` 永远是空的（shim 没有 `undeclare_parameter`）：

```bat
:: 窗口 A：盯着事件流（--count / --timeout 不写就一直跑）
call bridge\windows\bridge_env.bat
nav2\ros2_shim\build\parameter_events_probe.exe --count 5 --timeout 60

:: 窗口 B：改一个参数
bridge\windows\ros2.bat param set /controller_server min_x_velocity_threshold 0.5
```

```text
event 1
  stamp   55844.579996929
  node    /controller_server
  changed min_x_velocity_threshold = 0.5
---
```

`ros2 topic echo /parameter_events` 现在也打得出来：2026-09-17 起
`rcl_interfaces/msg/ParameterEvent` 进了桥的编解码表（证据 48），`node:` 与新增/变更的参数
都会列出来。加表之前它只会打印 `(N bytes of rcl_interfaces/msg/ParameterEvent)`，那时只能靠
`parameter_events_probe.exe` 用 shim 自己的类型解码 —— 那个工具现在仍然在，作为「不依赖编解码表」
的对照。整条栈起来的时候这条话题上有 14 个 publisher（每个默认构造的节点一个），写者是多个
进程共用一个环形缓冲 —— 见证据 39。

### 看日志：`/rosout`

2026-09-17 晚起，**每个节点自己也会发一条日志话题**（证据 50）：`Node` 构造末尾建
`rcl_interfaces/msg/Log` 的 publisher，`RCLCPP_INFO` 这类宏除 stderr 之外再往 `/rosout` 发一条，
带 `file` / `function` / `line`（指向上游源码里的调用点）。

```bat
:: 窗口 A：看 nav2 自己说的话
bridge\windows\ros2.bat topic echo /rosout --count 12

:: 窗口 B：造几条日志（不依赖编解码表的诊断工具，跟 parameter_events_probe 对称）
nav2\ros2_shim\build\rosout_probe.exe --count 5 --interval 0.5
```

不想让它发有两个开关：构造 `NodeOptions` 时 `enable_rosout(false)`（只关这一个节点），或者
`set NAV2_SHM_ROSOUT=0`（**整个进程**既不建 publisher 也不发）。三条要注意的：

* **一条 8 槽环、volatile**：`/rosout` 跟别的消息抢同一批槽（`NAV2_SHM_SLOT_COUNT` 是全话题
  共用的一个数），attach 之前发的日志读不到，日志多了会**挤掉别的消息**；
* **`NAV2_SHM_LOG_LEVEL` 是 stderr 与话题共用的一道闸门**，调大两边一起变大；
* `enable_rosout(false)` 的节点在 `ros2 topic info /rosout` 里看不出来（跟别的节点长得一样）。

跨机：把 Windows 的日志发到机器人那边（机器人侧要先用 `run_sim.py --subscribe /rosout` 起来，不用给类型）：

```bat
bridge\windows\ros2_bridge.bat start --uplink /rosout
```

类型会自己认出来（机器人图上先是没有类型，第一条消息过后变成 `rcl_interfaces/msg/Log`），
字段也解得出来（仿真包的 `messages.py` 里有 `Log` 的布局，对着 `rosbags` 逐字节验过）。
另外「建好之前的那几条读不到」—— 上行通道是守护进程看见本机有人发这个类型的
话题之后才建的：那之前发的会丢，之后的一条不差（证据 50 第九节）。

## 4. 验收 2：Windows 发布，Ubuntu 订阅

```bat
bridge\windows\ros2.bat topic pub /cmd_vel geometry_msgs/msg/Twist ^
    linear.x=0.25 angular.z=0.3 --rate 10 --duration 2
```

守护进程的上行转发线程在共享内存里看到 `/cmd_vel`，转成 CDR 后通过 `Publish` RPC 送到
Ubuntu agent，agent 注入话题图，仿真小车随即按速度积分。用 `/odom` 验证：

```bat
bridge\windows\ros2.bat topic echo /odom --count 1
```

实测：发 2 s、0.25 m/s 后小车位移 0.59 m，且 `/odom` 的 `twist.twist.linear.x` 变成 0.250。

**跑完记得让车停下**（仿真小车会一直沿用最后一条命令）：

```bat
bridge\windows\ros2.bat topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.0 angular.z=0.0 --once
```

上行话题默认是 `/cmd_vel`、`/goal_pose`、`/initialpose`，加上 nav2 回话的 `/plan` 与
`/behavior_tree_log` —— **验收那条「nav2 规划完把结果送回机器人」不用写任何参数**：
`ros2_bridge.bat start` 起守护进程、再起 nav2，`/plan` 与 `/behavior_tree_log` 自己就上去了
（名字的**类型**在 nav2 首次注册这个话题的下一个 `--refresh-ms` 里学到，见证据 54）。

要改这张表用 `--uplink`（可重复）或 `--no-uplink` 整个关掉。**在 `.bat` 里必须写成重复的形式**：
`cmd.exe` 会在参数到达批处理之前把逗号变成参数分隔符，`--uplink A,B` 只有 `A` 到得了守护进程。
想再加两张 costmap（定时重发、每条文几百 KB，所以没进默认表）就是这样：

```bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2_bridge.bat start --uplink /cmd_vel --uplink /plan --uplink /global_costmap/costmap
```

对端要有接住它的订阅者。`ros2_bridge_sim` 的 `--subscribe TOPIC[:TYPE]`（可重复）就是为这件事
加的：它在机器人那边注册一个订阅者，把每条消息按 `[sub] 话题 #序号 类型 <- 字段` 打到 stdout
（本侧没有 schema 的类型一样打，只是换成字节数），所以「Windows 发出去的东西到底有没有落地」
看 `/tmp/sim.log` 就行：

```text
[sub] /probe/tracking_feedback #1 nav2_msgs/msg/TrackingFeedback <- {"current_path_index": 3, "distance_to_goal": 4.5, "header": {"frame_id": "odom", ...}, ...}
[sub] /probe/speed_limit       #1 nav2_msgs/msg/SpeedLimit        <- 36 byte(s), no schema for nav2_msgs/msg/SpeedLimit on this side
```

（实测：4 个话题、28 条消息全部落地，逐条见证据 49。）

## 5. 验收 3：nav2 建图 + 规划 + 移动

```bat
bridge\windows\nav2_demo.bat --goal auto
:: 或者走 Python 包装（额外负责构建、起守护进程、导出报告）
E:\venvs\py312\Scripts\python.exe bridge\scripts\run_nav2_demo.py --goal auto --report run.json
```

流程：订阅 `/map` `/scan` `/odom` → 雷达射线投射维护占据栅格并与静态地图合并 →
距离变换 + nav2 `InflationLayer` 代价模型膨胀 → `nav2_navfn_planner::NavFn` 规划 →
发 `/plan` → 纯跟踪控制器 10 Hz 发 `/cmd_vel` → 用 `/odom` 判定到达并写 JSON 报告。

> 这一节是自己接的闭环。想跑**上游那套服务器本体**（`planner_server` + `controller_server` +
> `bt_navigator` + `lifecycle_manager`，行为树和规划器插件都是上游代码），跳到最后那一节。

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--goal X,Y` / `auto` | `auto` | 目标点；`auto` 取最远的可达空闲格 |
| `--map-source map\|laser\|union` | `union` | 用对端 `/map`、用 `/scan` 现场建图，或两者合并 |
| `--wait-topics SEC` | 30 | 等待 `/map` `/odom` `/scan` 出现的上限 |
| `--timeout SEC` | 180 | 超时放弃 |
| `--tolerance M` | 0.30 | 到点判定 |
| `--max-linear` / `--max-angular` | 0.6 m/s / 1.8 rad/s | 速度限幅 |
| `--lookahead M` | 0.7 | 纯跟踪前视距离 |
| `--rate HZ` | 10 | 控制周期 |
| `--replan SEC` | 3 | 全局重规划周期 |
| `--inscribed` / `--inflation` / `--cost-scaling` | 0.30 / 0.80 / 3.0 | 膨胀的致命半径、膨胀半径、代价缩放 |
| `--footprint M` | 0.35 | 规划前在机器人周围清空的名义半径 |
| `--plan-only` | 关 | 只发 `/plan`，不发 `/cmd_vel` |
| `--report FILE` | 无 | 写 JSON 总结 |
| `--no-topics` | 关 | 不打印启动时的 shim 图 |
| `-v` | 关 | 打印每个控制周期 |

`/plan` 也可以从另一个窗口观察：

```bat
bridge\windows\ros2.bat topic info /plan
```

### 一条命令起真正的 nav2 服务器栈（`nav2_stack.bat`）

上面那条 demo 用的是自己接起来的闭环（`nav2_bridge_demo`）。上游那套服务器本体，现在是一条命令：

```bat
rem 起栈，挑一个可达目标把车开过去，再把过程写成 JSON
bridge\windows\nav2_stack.bat --goal auto --report goal.json

rem 只起栈，留在后台接着用 ros2.bat 观察
bridge\windows\nav2_stack.bat

rem 收摊（不需要先设环境变量）
bridge\windows\nav2_stack.bat stop
```

同一个入口在 Linux 上是 `python3 bridge/scripts/run_nav2_stack.py`，**参数完全一样**，日志落在
`/tmp/nav2_stack/`；两边是同一份 Python（`bridge/scripts/run_nav2_stack.py`），`.bat` 只是给 cmd
习惯的外壳。Ubuntu 上「仿真在本机、守护进程连本机 agent」的完整跑法与实测数字见 `BUILD.md` 6.3。
#### 用一张存好的图导航（`--map FILE`）

`/map` 的默认来源是守护进程镜像进来的对端话题。要让栈读一张自己的图（`PGM` + `YAML`），加 `--map`
—— 这就是车上 `Run_nav.launch.py map:=FILE` 的等价物：

```bat
bridge\windows\nav2_stack.bat --map code\Desktop\racecar\map\ai_map001.yaml --goal 1.54,4.23 --timeout 90
```

脚本先起 `map_server` 并把它推到 ACTIVE，再起 AMCL：一张「存好的图」变成 `map -> odom` 的地方就是
AMCL。`/map` 只允许一个写者，所以守护进程正在镜像 `/map` 时脚本**拒绝启动**（`exit 2`），并把
`/map` 从镜像集里摘掉的那两条命令打出来：

```bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2_bridge.bat start --mirror /clock --mirror /odom --mirror /scan --mirror /tf --mirror /tf_static
```

Linux 上是同一对：`bash bridge/linux/ros2_bridge.sh stop` 与 `bash bridge/linux/ros2_bridge.sh start
--mirror /clock ...`。`start` 后面的话原样转给守护进程，和 `.bat` 一样 —— 这一点曾经不是这样：
`ros2_bridge.sh` 只把 `run` 剩下的参数递下去，`start --mirror /scan` 会把选项**念一遍然后丢掉**，
于是同一条被推荐过的命令在 Windows 上管用、在 Linux 上什么都没做（修在证据 65）。

`--map` 不能和 `--no-amcl` 一起用：存好的图正是 AMCL 要定位的对象。

注意 `%TEMP%\nav2_stack\map_server.log` 是唯一会说「图有多大」的地方（`Received a 480 X 480 map @
0.05 m/pix`），而栈起来之后 `/map` 的写者只有一个 —— `ros2.bat topic info /map` 应该显示 `1`。

它按这个顺序起七个进程，然后等生命周期管理器说话：

| 顺序 | 进程 | 作用 |
| --- | --- | --- |
| 1 | `amcl_bridge_node` | 发 `map -> odom`。**不能省**：两个 costmap 都要 `map -> base_link`，而这一段只有 AMCL 能给 |
| 2 | `planner_server` | 全局规划（NavFn / Smac），发 `/plan` |
| 3 | `controller_server` | 路径跟踪，发 `/cmd_vel`。四个控制器都装好了（RPP / DWB / MPPI / `rotation_shim`），默认 RPP —— 见下面「换控制器」 |
| 4 | `bt_navigator` | 行为树（`config\bt\*.xml`），把上面两个串成 `navigate_to_pose` 动作 |
| 5 | `behavior_server` | 行为树末尾恢复轮盘的 `Spin` / `Wait` / `BackUp`；**不能省**（见下） |
| 6 | `smoother_server` | `SmoothPath` action（`bt_navigator` 装的树里暂时没用到） |
| 7 | `waypoint_follower` | `FollowWaypoints` action，一个 `NavigateToPose` 接一个 |
| 8 | `lifecycle_manager` | 把前七个从 unconfigured 一路推到 active（上游靠 launch 文件干这件事） |

`behavior_server` 必须在 `bt_navigator` 激活之前就绪，而这不是「恢复分支降级」的问题：行为树是
**激活时**建的，任何一个叶子的 action server 不在，整个 `bt_navigator` 都激活不了
（`"spin" action server not available` → `Failed to bring up all requested nodes`）。
`lifecycle_manager` 的 `node_names` 里因此必须有它。

七个服务器都读上游的 `nav2_params.yaml`，Windows 侧只叠一层
`nav2\ros2_shim\config\nav2_shim_overrides.yaml`（`controller_plugins` 列三个控制器、
关掉 keepout / speed filter、补上 lifecycle_manager 的 `node_names`、把 `enable_stamped_cmd_vel`
钉成 `false`）。

「栈活了没有」不是靠 sleep 猜的，是读 `lifecycle_manager` 日志里的 `Managed nodes are active`：

```
[INFO] [lifecycle_manager] Configuring controller_server
[INFO] [lifecycle_manager] Configuring planner_server
[INFO] [lifecycle_manager] Configuring bt_navigator
[INFO] [lifecycle_manager] Activating controller_server
[INFO] [lifecycle_manager] Activating planner_server
[INFO] [lifecycle_manager] Activating bt_navigator
[INFO] [lifecycle_manager] Managed nodes are active
```

日志一节点一个文件，在 `%TEMP%\nav2_stack\<node>.log`（七个进程往一个控制台里写是没法看的）。

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--goal X,Y` / `--goal auto` | 不给 | 栈 active 之后交给 `nav2_goal` 跑；不给就只起栈 |
| `--timeout` / `--report` / `--yaw` / `--frame` / `-v` … | — | `nav2_goal` 的参数（见上一节的表），原样透传 |
| `--params FILE` | 上游 `nav2_params.yaml` | 换一份参数文件，overrides 仍然叠在上面 |
| `--no-amcl` | 关 | 已经有别的 `map -> odom` 时用，比如另一个窗口在跑 `slam_demo.bat` |
| `--duration S` | 无限 | 起起来的**节点**自己跑多久就走；不写就一直跑 |
| `stop` | — | 杀掉这七个进程 |

`--goal` 跑完脚本不会关栈（方便接着 `ros2.bat topic echo /cmd_vel` 看谁在发），要么 `stop`，
要么直接关窗口。

### 起栈之前先把小车放回原点

`amcl_bridge_node` 启动时会把 `/odom` 的当前位姿**当成 AMCL 的初始位姿**
（`amcl_bridge_node.log` 第一行就是 `initial pose from /odom: ...`）。所以：

```bat
rem 对：先复位，再起栈
bridge\windows\ros2.bat service call /sim/reset std_srvs/srv/Trigger
bridge\windows\nav2_stack.bat --goal auto
```

**顺序反了就一定跑不动**：栈起来之后再 `/sim/reset`，AMCL 不会跟着回到原点，`map -> odom`
一直偏着（实测偏到 `(-2.23, 12.01)`，而 480×480 @ 0.05 的 costmap 只有 `[-12, 12]`），
规划器对**每一个**目标都回

```
GridBasedplugin failed to plan from (-2.23, 12.01) ... :
"Start Coordinates of(-2.227350, 12.010600) was outside bounds"
```

这种状态**不重启栈不会自愈**。`acceptance_check.py` 收尾会自动复位。
（另有一个形状相似但是另一个成因的现象：AMCL 刚起来要几秒才收敛，冷启动的第一个目标会慢
或者失败一次 —— 那一条等十几秒就好，见 `03-validation.md` 的「未覆盖 / 已知限制」。）

### 换控制器：`/controller_selector`

三个控制器同时装在 `controller_server` 里（`FollowPath`=RPP、`DWBFollowPath`、`MPPIFollowPath`），运行期按名字切，**不用重启栈**：

```bat
rem 看有哪些
bridge\windows\ros2.bat topic echo /controller_selector --count 1

rem 切（值就是 controller_plugins 里的名字）
bridge\windows\ros2.bat topic pub /controller_selector std_msgs/msg/String data=DWBFollowPath --once
bridge\windows\ros2.bat topic pub /controller_selector std_msgs/msg/String data=MPPIFollowPath --once
bridge\windows\ros2.bat topic pub /controller_selector std_msgs/msg/String data=FollowPath --once
```

`controller_server.log` 里 `Selected controller: MPPIFollowPath.` 就是切换生效的证据
（要 `NAV2_SHM_LOG_LEVEL=debug`；实测见 `03-validation.md` 证据 27 / 30）。
`MPPIFollowPath` 的参数是从上游 `nav2_params.yaml` 的 `FollowPath` 原样搬过来的，
所以它跑的就是上游默认的 MPPI 那一套。

### 移植要点：这几处不修，栈起来了也不走

| 症状 | 原因 | 处理 |
| --- | --- | --- |
| 服务器一直看到 `0 subscribers`，`/cmd_vel` 静默丢包 | 订阅计数原来只数**本进程** | 图注册表加 `merged_topics_cached()`：跨进程合并之后再数（`include/ros2_shim/graph.hpp`），`bridge_daemon` 和 `ros2_cli` 也把自己的订阅登记进图 |
| 同上，而且发布者手里是 `cmd_vel`、图里却是 `/cmd_vel` | `create_publisher()` 把**相对名**原样交给了 `Publisher` | 两条创建路径（`rclcpp/factories.hpp`、`rclcpp_lifecycle/lifecycle_node.hpp`）都改成 `node.resolve_topic()` 之后再建 |
| 第一次跑通，重跑报 `the navigator refused the goal` 或 `the goal response never arrived` | 共享内存环在写进程退出之后还留着内容：新一轮客户端在环头读到**上一轮**的 ack；反过来，服务器起来时也会把**上一条**没人要的 goal 当新的读 | `rclcpp_action` 的 `Server` 构造时对 goal / cancel 两个环 `seek_to_latest()`；`Client` 构造时对 ack / cancel_ack / feedback / result 四个环做同样的事 |
| 重跑时脚本刚启动就宣布 active，紧接着目标被拒 | 上一轮的 `lifecycle_manager.log` 里还写着 `Managed nodes are active`，脚本一读就以为好了 —— 这时 `bt_navigator` 连动作服务器都还没建 | 每轮开局先删日志（`del /q %NAV2_STACK_LOG_DIR%\*.log`），且因为 `taskkill` 之后句柄释放有延迟，删是重试着删的 |
| AMCL 每条 scan 报 `frame 'base_footprint' does not exist`，两个 costmap 等 `base_link -> map` 超时，manager `Aborting bringup` | `-p` 排在 `--params-file` **前面**，被 `nav2_params.yaml` 里的 `base_frame_id: base_footprint` 盖掉 | `:spawn` 里参数文件一律排在 `-p` 之前 |
| `nav2_stack.bat --goal auto > run.log` 不返回，直到节点全退出 | `start /b` 的节点共用调用方的控制台，也就共用了它的句柄表 | 用 `start /min cmd /c ""exe" args > log 2>&1"`：节点各拿一个新控制台，日志仍然进文件 |

### 安全链：旁路 `nav2_safety.bat` / 进链 `nav2_stack.bat --safety`

上游 `nav2_bringup` 的 launch 文件把 `velocity_smoother` 与 `collision_monitor` 接在
**控制器和轮子之间**，靠的是 `remappings=[('cmd_vel', 'cmd_vel_nav')]`。本兼容层现在也有
`-r`（语义见证据 32），所以这两个节点有两种接法，选哪种由一条命令决定。

**旁路（观察用）** —— `nav2_safety.bat`，不动车：

```bat
bridge\windows\nav2_safety.bat                 :: 起两个节点并 activate，留在后台
bridge\windows\nav2_safety.bat --duration 60    :: 一分钟后自己退出
bridge\windows\nav2_safety.bat stop
```

Linux 上是同一个脚本：`python3 bridge/scripts/run_nav2_safety.py [--duration 60] [stop]`，
日志落在 `/tmp/nav2_safety/`。

```
velocity_smoother   /cmd_vel          -> /cmd_vel_smoothed
collision_monitor   /cmd_vel_smoothed -> /cmd_vel_monitored
```

`/cmd_vel_smoothed` 与 `/cmd_vel_monitored` 都没有第二个写者，**小车仍然听
`controller_server` 直接发的那条 `/cmd_vel`**，所以可以和安全节点同时跑而不用担心
车动得不对 —— 代价是这一层也管不了车。

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--duration S` | 无限 | 两个节点跑多久自己走 |
| `--params FILE` | `nav2\ros2_shim\config\nav2_shim_safety.yaml` | 换一份参数（仍然叠在上游 `nav2_params.yaml` 与 `nav2_shim_overrides.yaml` 之上） |
| `stop` | — | 杀掉这两个进程 |

**进链（真的能拦住车）** —— `nav2_stack.bat --safety`：

```bat
:: 栈 + 安全链一起起来，然后照常下发目标
bridge\windows\nav2_stack.bat --safety --goal auto --report goal.json

:: 想改限速或停止多边形时，再叠一份参数（最后加载，优先级最高）
bridge\windows\nav2_stack.bat --safety --safety-params C:\tmp\big_polygon.yaml --goal auto
```

```
controller_server   -> /cmd_vel_nav       -r cmd_vel:=cmd_vel_nav
behavior_server     -> /cmd_vel_nav       -r cmd_vel:=cmd_vel_nav
velocity_smoother   -> /cmd_vel_smoothed  -r cmd_vel:=cmd_vel_nav
collision_monitor   -> /cmd_vel           cmd_vel_out_topic
```

前三条与上游 launch 的 remap 一一对应（`velocity_smoother` 的输入话题写死在它源码里，
所以它必须 remap）；`collision_monitor` 的两个话题本来就是参数，只用
`nav2\ros2_shim\config\nav2_shim_safety_chain.yaml` 把输出从 `cmd_vel_monitored` 改回
`cmd_vel` —— 这也让它成为 `/cmd_vel` 在本机的**唯一写者**（共享内存一个通道只能有一个
写者，见证据 9）。这两个节点不在 `lifecycle_manager` 的 `node_names` 里，脚本自己在栈
ACTIVE 之后用 `/<node>/change_state`（transition 1 → 3）把它们推上去。

| 参数 | 说明 |
| --- | --- |
| `--safety` | 起 `velocity_smoother` + `collision_monitor` 并接进链；`stop` 会把它们一起收掉 |
| `--safety-params FILE` | 再叠一份节点参数，最后加载（改限速 / 停止多边形用），隐含 `--safety` |

观察（旁路与进链都能用）：

```bat
bridge\windows\ros2.bat topic echo /cmd_vel_nav
bridge\windows\ros2.bat topic echo /cmd_vel_smoothed
bridge\windows\ros2.bat topic echo /cmd_vel
```

日志：旁路在 `%TEMP%\nav2_safety\`，进链的这两个跟着栈进 `%TEMP%\nav2_stack\`。

> 两种模式不要同时开：那会给 `/cmd_vel_smoothed` 两个读者、给 `/cmd_vel` 两个写者。

### 航点链：`nav2_waypoints.exe`（规划 → 平滑 → 巡航）

`nav2_stack.bat --goal` 走的是 `bt_navigator` 那棵行为树（`navigate_to_pose`）。要把
`smoother_server` 与 `waypoint_follower` 也真正用起来，用另一个可执行文件（栈先起来）：

```bat
call bridge\windows\bridge_env.bat
nav2\ros2_shim\build\nav2_waypoints.exe --goal auto --count 3 --timeout 45 --report wp.json
````

它按 `ComputePathToPose` → `SmoothPath` → `FollowWaypoints` 的顺序发三个 action，航点取自
平滑后路径的 1/3、2/3 与终点，起点是 AMCL 定位出来的当前位姿。`--count N` 改航点数，
`--report FILE` 写 JSON（每段的点数、长度、最尖的拐角、`/cmd_vel` 计数）。实测连跑 6 次
全部 `SUCCEEDED`（证据 22）。

### 旁路：`nav2_docking_route.bat`（`docking_server` + `route_server`）

```bat
bridge\windows\nav2_docking_route.bat                 :: 起两个节点并各自 activate，留在后台
bridge\windows\nav2_docking_route.bat --duration 60    :: 一分钟后自己退出
bridge\windows\nav2_docking_route.bat --params FILE    :: 换参数（默认 nav2\ros2_shim\config\nav2_shim_docking_route.yaml）
bridge\windows\nav2_docking_route.bat stop

:: Linux 上同一份：python3 bridge/scripts/run_nav2_docking_route.py [同上]
````

日志在 `%TEMP%\nav2_docking_route\`（一节点一个文件）。两个节点都是 lifecycle 节点、
又都不在 `nav2_stack.bat` 的 `node_names` 里，所以脚本自己发 `/<node>/change_state`
把它们推到 ACTIVE：

```
[nav2_docking_route] docking_server is active
[nav2_docking_route] route_server is active
```

**它们目前只是「活着」**：`docking_server` 有 1 种 dock 类型、0 个实例，`route_server`
没有图文件（向它要路径会答 `No graph set! Aborting request.`）。要真用起来得提供 dock 位姿
或一份 GeoJSON 图，边界写在证据 28。

## 6. SLAM：在 Windows 上跑 slam_toolbox

`slam_toolbox`（2.8.5，humble 之后的版本）已经移植进兼容层，编译成 Windows **静态自包含** exe，运行时不
需要 MSYS2。它直接从共享内存图里读 `/scan` + `/tf`，所以除了守护进程以外没有别的依赖：

```bat
bridge\windows\ros2_bridge.bat start     :: 把 /scan /tf /tf_static /odom 镜像进来
bridge\windows\slam_demo.bat             :: 已预设 -p base_frame:=base_link
```

Linux 上是同一个脚本：`python3 bridge/scripts/run_slam_demo.py`（参数、`-p` 预置规则、让位规则都一样）。

跑起来之后：

```bat
bridge\windows\ros2.bat topic list                    :: 多出 /slam_map
bridge\windows\ros2.bat topic echo /slam_map --count 1
```

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `--mode sync\|async\|localization` | `sync` | 对应上游三个 node flavour |
| `--node-name` / `--namespace` | `slam_toolbox` / 无 | 节点名与命名空间 |
| `-p NAME:=VALUE` | 无 | 与 `ros2 run --ros-args -p` 同语法 |
| `--params-file FILE` | 无 | 直接读上游那份 YAML，可重复；见下 |
| `--ros-args` | 无 | 认这个分隔符并忽略，`ros2 run` 的命令行可以整条粘过来 |
| `--duration S` | 0 | 跑 S 秒后退出，0 表示一直跑到 Ctrl+C |
| `NAV2_SLAM_PARAMS` | `-p base_frame:=base_link` | `slam_demo.bat` 追加的参数；上游默认 `base_footprint`，本仓库的仿真是 `base_link`。命令行自己带了参数时这个预置会让位 |

**直接读上游的配置文件**（`nav2\third_party\slam_toolbox\config\`）：

```bat
bridge\windows\slam_demo.bat ^
    --params-file nav2\third_party\slam_toolbox\config\mapper_params_online_async.yaml ^
    -p base_frame:=base_link
```

文件按 `slam_toolbox:` 这个节点块读：带 `/**` 的通配块先应用、点名节点的块后应用，与 rclcpp 的
优先级一致；每个值同时以相对名和 `/slam_toolbox.<名字>` 两种键登记，所以上游配置一个字不用改。
`-p` 排在 `--params-file` 后面就覆盖文件里的同名项 —— 上面这条把文件里的 `base_footprint` 改回
本仓库仿真的 `base_link`，就是这个用法。

跑仿真器时最好把雷达量程调成和雷达一致，否则长距离行驶后地图会丢（默认 25 m，仿真雷达 12 m）：

```bat
bridge\windows\slam_demo.bat -p map_name:=/map -p max_laser_range:=12.0 -p map_update_interval:=2.0
```

仿真器会锁存最后一条 `/cmd_vel`，开完记得发一条零速度：

```bat
bridge\windows\ros2.bat topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.0 --rate 20 --duration 2
```

`/tf_static` 是 latched 的（仿真器每 5 s 重发一次），要等桥把它镜像进共享内存之后，`/scan` 的
`frame_id` 才能被查表。`slam_bridge_node` 会先等这件事发生再 configure/activate（最多 15 s，
`ros2_shim/lifecycle_runner.hpp` 里的 `waitForScanStaticTransform()`），启动后会打印
`/tf_static carries the laser frame`；所以冷启动不会再刷 `discarding scan`。

上游的 `slam_toolbox_*_node` 自己的 `main()` 只 spin，configure/activate 由外部的 lifecycle
manager 负责；这一个兼容层里没有 lifecycle manager，所以 `slam_bridge_node` 自己完成两步
转换（`ros2_shim/lifecycle_runner.hpp`），这也是直接跑上游 exe 会「什么都不订阅」的原因。

想把地图看成一幅图，用 `map_dump.exe`（`ros2 topic echo` 会把栅格截断成
`[N cells, M occupied]`，只看得到有没有、看不到好不好）：

```bat
call bridge\windows\bridge_env.bat
nav2\ros2_shim\build\map_dump.exe --topic /slam_map --settle 5 --out slam_map.pgm
```

它打印 `MAPDUMP frames=.. size=.. occupied=..`，写出 PGM，并在终端画一个 ASCII 预览。

仿真自己也发 `/map`。守护进程默认镜像远端全部话题，两者并存并不冲突（SLAM 写的是
`/slam_map`）；若想让 SLAM 独占 `/map`，重启守护进程时改用镜像白名单即可：

```bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2_bridge.bat start --mirror /clock --mirror /odom --mirror /scan --mirror /tf --mirror /tf_static
```

### 位姿图存档：存下来，下次接着建

地图（`/map`）存的是格子，位姿图存的是「车走过哪些位姿、它们之间有哪些约束」。想今天建一半、
明天接着建（lifelong / localization 模式），要的是后者。两个服务都由 `slam_toolbox` 自己伺服，
`ros2.bat` 直接调，**中间不经过守护进程**：

```bat
:: 存：写出 office.posegraph + office.data 两个文件，缺一不可
bridge\windows\ros2.bat service call slam_toolbox/serialize_map ^
    slam_toolbox/srv/SerializePoseGraph filename=C:/maps/office

:: 取法一：下一个进程起来的时候就把它读回来
bridge\windows\slam_demo.bat -p map_name:=/slam_map ^
    -p map_file_name:=C:/maps/office -p map_start_at_dock:=true

:: 取法二：对着一个已经在跑的节点调服务
bridge\windows\ros2.bat service call slam_toolbox/deserialize_map ^
    slam_toolbox/srv/DeserializePoseGraph filename=C:/maps/office match_type=1
```

```text
request:
filename: "C:/maps/office"
response:
result: 0 (success)          :: 0 成功，255 写不进去
```

`filename` 不用加引号（路径里没有空格就直接写），`--timeout S` 要改的话加在最后。

三个参数决定「读回来之后怎么用」：

| 参数 | 作用 |
| --- | --- |
| `map_file_name` | 有值就去找那份存档。**单独给它是没有用的**，见下 |
| `map_start_at_dock:=true` | 从图里第一个节点接上（等价 `match_type=1`），不要求给位姿 |
| `map_start_pose:=[x,y,theta]` | 从给定位姿接上（等价 `match_type=2`）。**证据 47 已验**：与上一条给出逐位相同的一张图。三个数要写成**浮点**（`0.0`，不是 `0`）—— `[0,0,0]` 是整数数组，读不成双精度数组，会被退回默认值并 WARN |

**`map_file_name` 单独给会被拒。** `shouldStartWithPoseGraph()`
（`nav2\third_party\slam_toolbox\src\slam_toolbox_common.cpp:579`）在文件名非空、但既没有
`map_start_pose` 也没有 `map_start_at_dock` 的时候打印

```text
[slam_toolbox] LocalizationSlamToolbox: Map starting pose not specified.
    Set either map_start_pose or map_start_at_dock.
```

然后**放弃解档**：节点不报错、照常起来，只是从一张空图重新开始建 —— 看起来像「读了但没读到」。
上面那条 `-p map_start_at_dock:=true` 就是为它准备的。

**列表值要写成浮点。** `-p` 的值现在跟 `ros2 param set` 走同一个解析函数（证据 47），所以
`-p "map_start_pose:=[1.5,-2.0,0.0]"` 是对的写法；写成 `[0,0,0]` 是**整数**数组，
喂给一个声明为双精度数组的参数会被退回默认值 —— 现在至少会先打一条说出参数名的 WARN
（`parameter 'map_start_pose' was overridden with the IntegerArray [0, 0, 0], which cannot be read
as the DoubleArray it is declared with: keeping the declared default`），再来才是 `slam_toolbox`
自己那条 `Map starting pose not specified`。在这之前两条错误长得一模一样，看不出是谁丢了。

用服务的时候 `match_type` 是**必填**的：`UNSET=0` / `START_AT_FIRST_NODE=1` /
`START_AT_GIVEN_POSE=2` / `LOCALIZE_AT_POSE=3`，给 `0` 会被
`deserializePoseGraphCallback()` 直接拒掉（
`Deserialization called without valid processor type set. Undefined behavior!`）。

两条路径都在证据 41 里跑过：解档回来的图与建图那趟逐像素只差 0.083% / 0.30%，差值来自解档之后
继续进来的新扫描；车一动不动的冷启动对照则是另一个尺寸（`210 x 449` 对 `322 x 473`）。

## 7. AMCL：在 Windows 上跑 nav2_amcl

`nav2_amcl`（navigation2 1.5.1）同样移植进兼容层并编成 Windows **静态自包含** exe：它订阅桥里镜像
进来的 `/scan` 与 `/map`，把粒子滤波结果发到 `/amcl_pose`，把 `map -> odom` 变换发到 `/tf`。

```bat
bridge\windows\amcl_demo.bat                  :: 用 /odom 定起点，一直跑到 Ctrl+C
bridge\windows\amcl_demo.bat --duration 120   :: 跑 120 s 后退出
```

Linux 上同一份：`python3 bridge/scripts/run_amcl_demo.py [--duration 120] [--initial-pose 1.5,-2,0]`。

不加参数时它拿 `/odom` 上的第一帧做初始位姿（等价于 `--initial-pose-from-odom`）；也可以自己给
（弧度，第三个值可省）：

```bat
bridge\windows\amcl_demo.bat --initial-pose 1.5,-2,0 -p laser_max_range:=12.0

:: 直接用 nav2 自己的参数文件（读的是里面的 amcl: 块）
bridge\windows\amcl_demo.bat ^
    --params-file nav2\nav2_extracted\navigation2-1.5.1\nav2_bringup\params\nav2_params.yaml ^
    -p base_frame_id:=base_link
```

`amcl_demo.bat` 预置的参数（用 `NAV2_AMCL_PARAMS` 追加或覆盖；命令行自己带了
`-p`/`--param`/`--params-file` 时预置会让位，免得和 `--params-file` 里的同名项打架）：

| 参数 | 值 | 为什么 |
| --- | --- | --- |
| `-p base_frame_id:=base_link` | `base_link` | 上游默认 `base_footprint`，本仓库的仿真车是 `base_link` |
| `-p laser_max_range:=12.0` | `12.0` | 上游默认 100 m，会把「什么都没打到」的光束当成有效回波；仿真雷达 `range_max` 是 12 m |
| `-p save_pose_rate:=0.0` | `0.0` | 兼容层没有参数持久化，关掉免得刷错 |
| `-p first_map_only:=true` | `true` | 仿真的 `/map` 是 latched transient_local，只该取第一张 |

跑起来之后：

```bat
bridge\windows\ros2.bat topic list -t              :: 多出 /amcl_pose、/particle_cloud、/initialpose
bridge\windows\ros2.bat topic echo /tf --count 8   :: 里能看到 map -> odom 和 odom -> base_link
```

跟得准不准别靠眼睛猜，用 `amcl_probe.exe` 量：

```bat
call bridge\windows\bridge_env.bat
nav2\ros2_shim\build\amcl_probe.exe --duration 60 --period 3
```

它在同一个进程里同时订阅 `/scan`、`/odom`、`/amcl_pose`，把 AMCL 的估计和**两个时刻**的里程计
比对：`err_now` 对最新里程计，`err_at_amcl_stamp` 对「AMCL 打时间戳那一刻」的里程计；两者之差
就是估计年龄（`amcl_age`）。它能把「估计算错了」和「估计只是比里程计旧」分开 —— 实测里多出来
的误差几乎全是后者（见 `03-validation.md` 证据 5）。

上游的 `amcl` 可执行文件只 spin，configure/activate 由 lifecycle manager 驱动；这里没有 manager，
所以 `amcl_bridge_node` 自己完成两步转换。

## 8. 地图的存取：`nav2_map_server` + `map_saver`

上游的 `nav2_map_server` 卡在 GraphicsMagick 上：`map_io.cpp` 用 Magick++ 读写图片。
移植时没有把 GraphicsMagick 拖进来，而是自己写了一个替身 `Magick++.h`
（`nav2\ros2_shim\include\Magick++.h`），上游那个文件一个字没改。替身认两种格式：
**PGM**（`P5` / `P2` 进、`P5` 出 —— PGM + YAML 本来就是 ROS2 地图的事实标准，也是 `map_saver`
在 `trinary` 模式下的默认）和 **8 位 PNG**（`mode: scale` 要透明通道，`map_saver` 恰好在这一档
默认 `image_format: png`，`slam_toolbox` 的默认配置里写的也是它）。压缩用工具链自带的 zlib，
`-static` 让 `libz.a` 跟着进 exe，不新增需要带在身边的 DLL。

三个可执行文件：

| 程序 | 干什么 |
| --- | --- |
| `map_saver_cli.exe` | 订阅一个话题、存一次、退出。等价于 rviz2 里的「Save Map」 |
| `map_server.exe` | 读 YAML + PGM / PNG，把它作为 `/map` 发布（latched），并开 `load_map` / `get_map` 服务 |
| `map_saver.exe` | 生命周期节点，`save_map` 服务版的地图存档；和 `map_server` 共用 `config\nav2_map_server.yaml` |

### 存：把 `/map` 落到磁盘

```bat
call bridge\windows\bridge_env.bat
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt pgm
```

`-f` 是不带扩展名的路径，出来的是 `office.pgm` 加一个 `office.yaml`。

```
[INFO] [map_io] Received a 480 X 480 map @ 0.05 m/pix
[INFO] [map_io] Writing map occupancy data to C:\maps\office.pgm
[INFO] [map_io] Writing map metadata to C:\maps\office.yaml
[INFO] [map_io] Map saved
```

`--fmt png` 走的是另一个 coder：`trinary` / `raw` 写 8 位灰度 PNG，`scale` 写带 alpha 的那种
（透明格子 = `map_io` 判定的「未知」，读回来仍然是未知）。存档名跟着变成 `office.png`，
任何只认 PNG 的工具都能直接打开 —— 这就是从前需要多转一次格式的那件事。

### 取：把磁盘上的地图变回 `/map`

```bat
call bridge\windows\bridge_env.bat
nav2\ros2_shim\build\map_server.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml ^
    -p yaml_filename:=C:\maps\office.yaml
nav2\ros2_shim\build\map_saver.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml
nav2\ros2_shim\build\lifecycle_manager.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml
```

`nav2_map_server.yaml` 里的 `lifecycle_manager.node_names` 就是这两个节点、`autostart: true`，
所以第三条命令把前两个一起拉起来。`-p` 必须排在 `--params-file` **后面**：参数按出现顺序生效，
否则文件里的 `yaml_filename: ""` 会盖掉命令行给的那个。

```bat
bridge\windows\ros2.bat topic info /map
:: Publisher count: 1 / Source: local (shared memory)
bridge\windows\ros2.bat topic echo /map --count 1
:: info: {resolution: 0.05, width: 480, height: 480, origin: {x: -12, y: -12}}
:: data: [230400 cells, 11529 occupied]
```

### 一个话题只有一个写者，所以桥得让位

`/map` 被镜像进来的时候写者是守护进程；`map_server` 一发，同一个通道就有两个写者了。
要在「地图来自文件」的配置下跑，就让守护进程别镜像 `/map`：

```bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2_bridge.bat start --mirror /scan --mirror /odom --mirror /tf ^
    --mirror /tf_static --mirror /clock
```

这时雷达和位姿仍然来自车、地图来自文件，AMCL 订阅的就是 `map_server` 那张：

```
[INFO] [amcl] Subscribed to map topic.
[INFO] [amcl] Received a 480 X 480 map @ 0.050 m/pix
```

`ros2 topic info /map` 这时是 `Publisher count: 1 / Subscription count: 1` —— 那个订阅者在另一个
进程里（跨进程的发布/订阅计数见 `01-architecture.md`）。

### 一条回路：存下来，再读回去

这两半合起来是可以自证的 —— 存一次、读回来、再存一次，两次的文件应当逐字节相同
（`.scratch\map_roundtrip.py`）：

```bat
call bridge\windows\bridge_env.bat
bridge\windows\ros2_bridge.bat stop
bridge\windows\ros2_bridge.bat start --mirror /scan --mirror /odom --mirror /tf ^
    --mirror /tf_static --mirror /clock
nav2\ros2_shim\build\map_server.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml ^
    -p yaml_filename:=C:\maps\office.yaml
nav2\ros2_shim\build\map_saver.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml
nav2\ros2_shim\build\lifecycle_manager.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml
nav2\ros2_shim\build\map_dump.exe --topic /map --settle 5 --out C:\maps\back.pgm
:: back.pgm 与 C:\maps\office.pgm 应当逐字节相同
```

**行序**：ROS 地图文件的第一行是 y **最大**的那一行，而 `OccupancyGrid` 的第一行是 y
最小的那一行，两边各翻一次 —— `map_saver` / `map_server` / `map_io.cpp` 都是这么做的，
`map_dump.exe` 从前不是：它照格子的行序写，出来的 PGM 是上下翻的，交给 AMCL 会让整张
地图沿 x 轴镜像。现在它写的 PGM 与 `map_saver_cli` 的逐字节一致（证据 36）。

### 这个 Magick++ 替身覆盖到哪

| | |
| --- | --- |
| 能读 PGM | `P5`（二进制）与 `P2`（ASCII）；`maxval` 不是 255 时按比例落到 0..255，所以别的工具写的 15 位地图也能读 |
| 能读 PNG | 8 位、非交错，五种颜色类型都认：灰度、RGB、调色板、灰度+alpha、RGBA；五种行过滤器（None / Sub / Up / Average / Paeth）都实现；`gAMA` / `tEXt` / `pHYs` 这类不影响像素的块跳过。RGB / RGBA / 调色板按 Rec.601 亮度落到灰度（`(299r+587g+114b+500)/1000`） |
| 能写 | `P5`（maxval 255）；PNG 写灰度或灰度+alpha —— 调用方建了 alpha 通道就写带 alpha 的那种，没建就写纯灰度 |
| 透明通道 | **PNG 有，PGM 没有**。`mode: scale` 要的就是透明，所以 `map_saver` 在这一档默认 `png`；存成 `pgm` 会丢透明，上游自己也会为此警告 |
| 不能 | `bmp` / `jpeg`、交错 PNG、位深不是 8 的 PNG、五种之外的颜色类型：`CoderInfo` 报「没有这个 coder」，`map_saver` 于是按上游的 fallback 改成 `png`（日志里是 `Format 'bmp' is not usable. Using 'png' instead`），而不是写出一个解码错的图 |
| 三种 `mode` | `trinary` 与 `raw` 能往返：`trinary` 写 254 / 205 / 0（空闲 / 未知 / 占据），YAML 里把阈值固定成 `free_thresh: 0.196`、`occupied_thresh: 0.65` 就是为了让 205 读回来还是未知；`raw` 直接把 0..100 写进像素，读回来逐个相等。`ctest` 里的 `nav2_map_server_map_io` 测的就是这些，PNG 的两条（灰度往返、`scale` 模式的 alpha 往返）也在里面（证据 35） |

## 9. 一条命令跑完验收

```bat
E:\venvs\py312\Scripts\python.exe bridge\scripts\acceptance_check.py
```

依次执行：冷启动 `topic list -t` → `/scan` 来源检查 → `/scan` 频率 → 上行 `/cmd_vel` →
nav2 demo → SLAM（建图一圈 **+** 存档换进程读回来定位），逐项打印 PASS/FAIL，并按 `--report`（默认
`bridge\docs\validation\acceptance-report.json`）写出 JSON。`--quick` 跳过 nav2 和 SLAM，
`--skip-slam` 只跳过 SLAM，`--keep-daemon` 不做冷启动。

最后那条 SLAM 检查（`check_slam`）是六条里唯一会改守护进程配置的：它先停掉守护进程、
用**不带 `--mirror /map`** 的镜像表重启（这样 `slam_demo.bat` 才能自己发 `/map`），
把车复位、起 SLAM、开一圈「4 × 前进 2 m 转 90°」的闭环，再用 `map_dump.exe` 读回
占用格数，收尾把默认守护进程拉回来。实测 65 s 一圈，`occupied=384`（证据 45）。

## 10. 环境变量

`bridge_env.bat` 设置的全部变量（都可用「先设置、后调用」的方式覆盖）。Linux 侧的
`bridge/linux/bridge_env.sh` 设的是同一套变量、同一批默认值，只有路径拼法不同（`bridge\windows` →
`bridge/windows`、没有 `.exe`）；下面 Windows 专有的两个（`ROS2_BRIDGE_MSYS2`、
`ROS2_BRIDGE_NO_MSYS2_PATH`）它不设：

| 变量 | 默认 | 作用 |
| --- | --- | --- |
| `ROS2_BRIDGE_CPU_ROOT` | 仓库根 | 别的脚本定位 `gRPC/`、`nav2/` 用 |
| `ROS2_BRIDGE_MSYS2` | `D:\WindowsServer2025_1\software\MSYS2` | 工具链/gRPC 来源（只在构建时需要；运行时 DLL 已经在 `build\` 里） |
| `ROS2_BRIDGE_NO_MSYS2_PATH` | 空 | 置 1 时不把 MSYS2 的 `bin` 加进 PATH |
| `ROS2_BRIDGE_BUILD` | `bridge\windows\build` | 守护进程与 CLI 的构建目录 |
| `NAV2_SHM_SLOT_COUNT` | 8 | 共享内存环形缓冲的槽数 |
| `NAV2_SHM_SLOT_SIZE` | 2097152 | 普通话题的槽大小（2 MB） |
| `NAV2_SHM_LARGE_SLOT_SIZE` | 8388608 | 名字含 `map`/`costmap`/`voxel`（但不含 `info`）的话题用 8 MB |
| `NAV2_SHM_NAMESPACE` | 空 | 通道名前缀：`test` → `ros2shimtest/scan`，同时给图注册表目录加后缀。用来把互不相干的两套栈分开（测试 vs 活着的桥） |
| `NAV2_SHM_GRAPH_DIR` | `%TEMP%\nav2_shim_graph` | 图注册表目录（shim 真正读的名字）；设了 `NAV2_SHM_NAMESPACE` 时实际路径是 `<这个值>_<命名空间>` |
| `ROS2_SHM_GRAPH_DIR` | 同上 | 旧脚本用的别名 |
| `NAV2_SHM_LOG_LEVEL` | `info` | shim 的 rclcpp 兼容日志级别：`debug\|info\|warn\|error\|fatal`。**同时管着 stderr 与 `/rosout` 两条路** |
| `NAV2_SHM_ROSOUT` | 空（= 开） | 置 0 时整个进程**不建** `/rosout` publisher、`RCLCPP_*` 也不上话题（stderr 照旧）。槽大小走 256 KB，和 `/parameter_events` 一样 |
| `ROS2_GRPC_SDK_CONFIG_DIR` | `gRPC\ros2_gRPC_sdk\config` | gRPC SDK 的发现配置 |
| `ROS2_BRIDGE_AUTOSTART` | 1 | 置 0 时 `ros2.bat` / `nav2_demo.bat` 不再自动拉起守护进程 |
| `ROS2_BRIDGE_READY_FILE` | `build\ros2_bridge_daemon.ready` | 守护进程「首次连接尝试结束」的标记文件，`wait_bridge_ready.bat` 轮询它 |
| `ROS2_BRIDGE_WAIT_MS` | 30000 | 上面那个标记文件的等待预算（毫秒），0 = 不等待 |
| `ROS2_BRIDGE_LOG` / `ROS2_BRIDGE_ERR` | `build\ros2_bridge_daemon.log` / `.err.log` | 分离模式下守护进程的 stdout / stderr |
| `ROS2_BRIDGE_LOG_MAX` | 16777216（16 MiB） | 分离模式日志的大小上限（`--log-max-bytes` 同义）。超过就把 `<path>` 换成 `<path>.1` 再开一个空的，稳态是两个文件、合计不超过 2× 这个值；**0 = 关掉检查**。`--detach` 才查（非分离时 stdout 是调用者的控制台，不能动）—— 扫描重试期间与控制循环里都会查（证据 63） |
| `NAV2_SHIM_BT_DIR` | `nav2\ros2_shim\config\bt` | `bt_navigator` 用的行为树 XML 目录（上游默认树里有三个叶子依赖没移植的 `nav2_behaviors`，所以这里放的是降级副本，见 03 证据 18） |
| `ROS2_SHIM_ACTION_TRACE` | 空 | 置 1 时 shim 的 action 层把每条 goal/ack/feedback/result 打到 stderr（每个节点的 `.log`），排查「目标发出去没反应」用 |
| `NAV2_SHIM_BT_TRACE` | 空 | 置 1 时 `bt_navigator` 每 30 拍打一行行为树里每个节点的状态：`[bt-trace] ticks=N <节点>=<STATUS> …`；排查「目标被接受、树却不动」用（证据 26） |
| `NAV2_SHM_SUB_DEBUG` | 空 | 值是一条话题名（或 `*`）时，只给这条话题的订阅打收发日志：谁在读、读到了什么 |

冷启动 `ros2 topic list` 曾经靠「固定睡一段时间、希望扫描已经跑完」，机器慢一点就变
成空列表。现在改成确定性等待：守护进程在**第一次连接尝试结束时**写
`ROS2_BRIDGE_READY_FILE`（成功 = `/scan` 这些话题已经在共享内存里；没找到 agent =
本来就没什么可等的），等待期间文件不存在，所以「文件在」永远等价于「图是稳的」。

这个文件的内容是

```
status=ready peer=192.168.1.113:50051 topics=7 services=1 actions=1 mirrors=6/6
```

`status` 取 `ready` / `no-agent`；`mirrors=<活着的镜像>/<总数>` 是后加的，配套的规则也变严了：

* **镜像不齐时文件会被删掉**，不是留着一个过时的 `ready`。gRPC 流断开的那条话题的镜像会
  标成不活，文件随之消失；重连成功后（`reconnects=N` 出现在同一行里）再写回来。
  所以「等它出现」依然是安全的等待条件，只是它现在可以**反复出现**，不只是启动一次。
* `ros2_bridge.bat stop` 会把它删掉，免得下一次启动的脚本看到上一轮留下的「就绪」。
* 实测：杀掉 Ubuntu 仿真 → `status=no-agent mirrors=0/6`；重新起仿真 → 守护进程自己重订阅，
  出现 `reconnects=6`，`ros2 topic echo /scan` 立刻恢复正常（03 证据 17）。

只在调用处生效的额外变量：

| 变量 | 作用 |
| --- | --- |
| `ROS2_BRIDGE_AGENT=192.168.1.113:50051` | 跳过扫描，直连指定 agent |
| `ROS2_BRIDGE_DAEMON_ARGS="--mirror /scan"` | 给守护进程追加参数 |
| `NAV2_SLAM_PARAMS` | `slam_demo.bat` 追加的参数（默认 `-p base_frame:=base_link`；调用方自己提到 `base_frame` 或用了 `--params-file` 时让位） |
| `NAV2_SLAM_EXE` | 覆盖 `slam_demo.bat` 要跑的 node 可执行文件 |
| `NAV2_AMCL_PARAMS` | `amcl_demo.bat` / `run_amcl_demo.py` 追加的参数（默认见第 7 节；调用方自己提到 `base_frame` 或用了 `--params-file` 时让位） |
| `NAV2_AMCL_EXE` | 覆盖 `amcl_demo.bat` / `run_amcl_demo.py` 要跑的 node 可执行文件 |
| `ROS2_SIM_ROOT` / `ROS2_SIM_LOG` | Ubuntu 控制脚本的工作目录 / 日志路径 |

**共享内存几何必须一致**：所有进程要么都经过 `bridge_env.bat`，要么都经过
`run_nav2_demo.py` 导出的环境。两边不一致时表现为「话题在、消息是空的」。

## 11. 守护进程参数

```
ros2_bridge.bat start [--agent HOST:PORT] [--mirror REGEX] ...
ros2_bridge.bat run   [...]        :: 前台运行，Ctrl+C 停止
```

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--agent HOST:PORT` | 无 | 直连，跳过局域网扫描 |
| `--ports P[,P...]` | 50051 | 扫描的端口 |
| `--subnets CIDR[,...]` | 所有本地子网 | 扫描范围 |
| `--discover-timeout MS` | 3000 | 发现总预算 |
| `--loopback` | 关 | 把 127.0.0.1 也纳入扫描 |
| `--mirror REGEX` | 全量 | 只镜像匹配的话题（可重复） |
| `--action REGEX` | 全量 | 只转发匹配的远端 action（可重复） |
| `--no-actions` | 关 | 完全关掉 action 的镜像与转发 |
| `--uplink NAME[,NAME...]` | `/cmd_vel,/goal_pose,/initialpose,/plan,/behavior_tree_log` | 哪些本机话题转发上行；**可重复**，第一条替换默认表、后面的继续追加（`.bat` 里逗号会被 `cmd.exe` 吃掉，所以重复写法才可靠）。名字学不到类型时静默跳过，所以默认表里多列两个 nav2 名字不花钱 |
| `--no-uplink` | 关 | 完全关闭上行 |
| `--throttle-ms MS` | 0 | 丢帧节流 |
| `--refresh-ms MS` | 2000 | 话题重扫周期 |
| `--retry-ms MS` | - | 没有 agent 时的重试周期（0 = 放弃） |
| `--duration S` | 0 | 跑 S 秒后退出（0 = 常驻） |
| `--once` | 关 | 扫描/同步一次、打印图后退出 |
| `--print-topics` | 关 | 启动时打印每个被镜像的话题 |
| `--detach` | 关 | 后台模式：输出追加到日志文件并释放控制台 |
| `--log` / `--log-err PATH` | 见上表 | 分离模式的日志路径 |
| `--config PATH` | `$ROS2_GRPC_SDK_CONFIG_DIR` | 显式指定 SDK 配置 |

## 12. Ubuntu 侧工具

```bat
:: 把本地的 ros2_bridge_sim 同步过去（tar over ssh，一条命令、保留权限）
E:\venvs\py312\Scripts\python.exe bridge\scripts\sync_to_ubuntu.py [--run]

:: 在机器人上执行任意 shell（stdin -> bash -s，引号不会被改写）
E:\venvs\py312\Scripts\python.exe bridge\scripts\ubuntu_ssh.py "tail -n 20 ~/sim.log"

:: 起停仿真小车 + gRPC agent
E:\venvs\py312\Scripts\python.exe bridge\scripts\ubuntu_ssh.py --file bridge\scripts\ubuntu_sim_ctl.sh -- start
E:\venvs\py312\Scripts\python.exe bridge\scripts\ubuntu_ssh.py --file bridge\scripts\ubuntu_sim_ctl.sh -- status
E:\venvs\py312\Scripts\python.exe bridge\scripts\ubuntu_ssh.py --file bridge\scripts\ubuntu_sim_ctl.sh -- stop
```

`ubuntu_sim_ctl.sh` 会在 `$HOME/sim.log` 留日志、在 `$HOME/.ros2_bridge_sim.pid` 记 pid；
它把不认识的参数原样转给 `run_sim`，所以 `-- start --subscribe /probe/x:std_msgs/msg/Bool`
也能用。`run_sim` 自己的参数：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--host` / `--port` | 0.0.0.0 / 50051 | gRPC agent 的监听地址 |
| `--seed` | 随机 | 内部障碍物的随机种子 |
| `--rate` | 50 Hz | 仿真步进频率 |
| `--goal X,Y[,THETA]` | 无 | 给一个目标，让内置控制器自己开过去 |
| `--no-console` | 关 | 不启动交互控制台（常驻时用） |
| `--duration S` | 0 | 跑 S 秒后退出 |
| `--subscribe TOPIC[:TYPE]` | 无 | 额外订阅（可重复），每条消息打成 `[sub] 话题 #序号 类型 <- 字段`；给「从 Windows 推过来的话题」做落点，见第 4 节 |
| `-v` | 关 | debug 日志 |

## 13. 排错

| 症状 | 先看这里 |
| --- | --- |
| `ros2.bat` 调用后卡住不返回 | 守护进程必须带 `--detach` 启动（`ros2_bridge.bat start` 已经这么做了）。自己手起的 daemon 如果继承了管道句柄，被重定向的调用会一直等它退出 |
| `topic list` 是空的 | agent 没跑（`ubuntu_sim_ctl.sh status`）、端口不通，或扫描没扫到。看 `bridge\windows\build\ros2_bridge_daemon.log`，或先 `ros2_bridge.bat run --print-topics` 前台观察 |
| 跑 `ctest` 时 SLAM 结果乱飘（drift 几米、房间尺寸也不对） | 守护进程正在镜像相同的 `/scan`、`/tf`，而一个共享内存通道只能有一个写者。要么 `ros2_bridge.bat stop` 再测，要么 `set NAV2_SHM_NAMESPACE=citest` 把测试的通道隔离出去（详见 `03-validation.md` 证据 9） |
| 话题在，但 `echo` 没有内容 | 共享内存几何不一致，或该话题没有发布者。确认所有进程都经过 `bridge_env.bat` / `run_nav2_demo.py` |
| `cannot open '<topic>': channel does not exist` | 话题名写错了。`topic pub` / `topic echo` 对多余参数会直接报 `unexpected argument` |
| nav2 demo 起来就报找不到话题 | 默认 `--wait-topics 30`，确认守护进程在跑（`ros2_bridge.bat status` 应显示 7 个远端话题） |
| 小车停不下来 | 发一条零速：`ros2 topic pub /cmd_vel geometry_msgs/msg/Twist linear.x=0.0 angular.z=0.0 --once` |
| 车一直往前开，`ros2_bridge.bat stop` 也停不下来 | **有一个漏掉的发布者还在发**。`ros2 topic pub` 不带 `--once` / `--duration` 就是按 `--rate` 一直发（和真 ROS2 一样），而 Windows 上杀父不杀子：`Popen` 了 `cmd /c` 之后只 kill cmd，`ros2.exe` 会活下来继续发，仿真那边的看门狗也就永远不跳 —— 它防的是「没人发」，不是「有人在发」。实测漏掉一个 `linear.x=0.1` 的发布者，小车一路开到 **x=22.4 m**；`taskkill /F /IM ros2.exe` 之后下一个周期就 `cmd=(0.00, 0.00)`。`acceptance_check.py` 两处发布都用 `taskkill /F /T` 收整棵树，就是为了这个 |
| 每一个目标都被规划器拒：`Start Coordinates of(..., ...) was outside bounds` | 起栈时小车不在原点，AMCL 按当时那个位置定了位，之后 `/sim/reset` 也救不回来。停栈 → 复位 → 再起栈（见「起栈之前先把小车放回原点」） |
| nav2 的 DEBUG 日志刷屏 | `NAV2_SHM_LOG_LEVEL=warn` |
| `slam_demo.bat` / `amcl_demo.bat` 一直刷 `Failed to compute odom pose` / `frame 'base_footprint' does not exist` | 节点在用上游默认的 `base_footprint`，而仿真的车只有 `base_link`。脚本自带 `-p base_frame:=base_link`（AMCL 是 `base_frame_id`），只要命令行里出现 `base_frame` 或 `--params-file` 它就让位——那就自己补一句，见第 6 节 |
| `--params-file` 读了上游配置，SLAM/AMCL 却收不到雷达 | 上游配置里是 `base_frame: base_footprint`、量程 25 m / 100 m，本仓库的仿真是 `base_link` / 12 m。在 `--params-file` **后面**补 `-p base_frame:=base_link`（AMCL 是 `base_frame_id`）之类的覆盖 —— 后面的参数赢 |
| `--params-file` 说 "0 parameter(s)" | 文件的顶层键不是这个节点名。SLAM 找的是 `slam_toolbox`、AMCL 找的是 `amcl`；键可以写 `slam_toolbox:`、`/slam_toolbox:` 或 `/**:` |
| 脚本里 `cmd /c "call bridge_env.bat && %ROS2_BRIDGE_CPU_ROOT%\...\xxx.exe"` 报 `The system cannot find the path specified.` | `bridge_env.bat` 是设了那个变量，但 **cmd 在解析整行的时候就展开 `%VAR%`**，那一刻 `call` 还没跑。直接写绝对路径，或开 `enabledelayedexpansion` 用 `!VAR!` |
| 同上，这次是包了引号的 exe 绝对路径 | `cmd /c` 在字符串里引号多于两个时，会去掉开头的引号和**最后一个**引号，而最后一个正是 exe 路径的收尾引号。脚本里一律用 argv 列表（`subprocess.run(["cmd", "/c", *args])`），别用 `shell=True` 拼字符串 |
| `build_bridge.py --target ros2` 报 `cannot open output file ...\ros2.exe: Permission denied` | 有个 `ros2.exe` 还活着（Windows 锁住正在运行的 exe）。最常见的是手工跑的 `ros2 topic hz`，它不自己退出；其次是**某个脚本 `Popen` 了 `cmd /c` 之后只 kill 了 cmd** —— Windows 上杀父不杀子。`tasklist /fi "imagename eq ros2.exe"` 看一眼，或者 `taskkill /F /IM ros2.exe` |
| `ros2 node list` 里出现重复的 `/ros2_cli_sub` | 同一个原因：漏掉（或硬杀）的 CLI 进程留下的注册项。图注册表按 PID 判活，进程真没了之后下次读图就清掉了 —— 两个卡住的 `ros2 topic hz` 在的时候，`node list` 里就是两个 `/ros2_cli_sub`。`acceptance_check.py` 的 `scan_rate` 曾经每次都漏一个，现在它用 `taskkill /F /T` 收整棵树 |
| 怎么彻底停掉一切 | `ros2_bridge.bat stop` + `ubuntu_sim_ctl.sh stop` |

## 14. 测试

```bat
:: CDR <-> 共享内存 的字节级转码自检
E:\venvs\py312\Scripts\python.exe bridge\scripts\build_bridge.py --test

:: Ubuntu 侧仿真/agent/图注册表的单元测试（纯 Python，在 Windows 上就能跑；61 条）
cd bridge\ubuntu && E:\venvs\py312\Scripts\python.exe -m pytest ros2_bridge_sim\tests -q

:: 兼容层 + 魔改 nav2（32 条，清单见 03-validation 的「单元与一致性测试」）
:: 每条测试自己带一个通道命名空间（CMakeLists.txt 末尾的 ENVIRONMENT_MODIFICATION，
:: 见证据 20），所以守护进程在跑也照样能跑，不必再手动设 NAV2_SHM_NAMESPACE；
:: 图注册表也跟着那个命名空间走，所以并发跑也不会互相看见对方的节点
D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin\ctest.exe --test-dir nav2\ros2_shim\build --output-on-failure
```

串行跑每次都 32/32（**52.27 s**，32 条那次实测）。29 → 31 的那两条是**补注册**、不是新写的：
`ros2_shim_py_interop`（3.39 s，五条腿的跨语言矩阵，驱动脚本 `nav2\ros2_shim\python\interop_matrix.py`）与 `ros2_shim_nav2_ros_common`（0.04 s）—— 两条都**编了很久、却一直没有 `add_test`**（其中 `ros2_shim_nav2_ros_common` 当时连断言
都没有，同轮补成 15 条并做了变异验证），起因与实测见 `05-audit.md` 第二、三节。
（`ros2_shim_rosout` 的 0.65 s 是更早那次 28 → 29 加的。）**加了 `-j` 现在也可以**，但还不是无条件：

```bat
:: 50~100 轮的压力循环，每轮的失败用例、耗时和日志都记下来
E:\venvs\py312\Scripts\python.exe .scratch\ctest_stress.py --rounds 50 --jobs 8
```

实测的数字：图目录跟着命名空间走之前，`-j 8` 六轮里 **0 轮**干净；之后 40 轮里 32 轮、30 轮里
22 轮干净。**2026-09-17 再跑十轮：7 轮干净**（同日更晚、31 条时再跑 5 轮：**5/5 干净**，
每轮 19.5-22.8 s，见 `05-audit.md` 3.2 —— 数字是新的，但样本太小，还不足以把上面这句推翻），剩下三轮三个不同的人 ——
`ros2_shim_graph_rewrite`（**一个读数没能打开文件**，1 295 次读里 1 次
`ERROR_SHARING_VIOLATION`；空白 **0** 次、半截 **0** 次）、`slam_toolbox_mapping`
（`activate()` 之后立刻断言订阅数 > 0，撞上 250 ms 的计数缓存）、以及 `shm_transport`
**挂到 900 s 超时被杀**。所以「唯一还挂的是 `ros2_shim_graph_rewrite`」这句已经过期。
三种形状的成因和修法在证据 42 和 `04-gaps.md` 第七节。

**这三种里现在只剩第三种没修**（2026-09-17 当天改掉两种，改法、A/B 和复测都在证据 43、
`04-gaps.md` 第七节末与第九节）：

* `shm_transport` 那种挂法已经不可能再出现：`publish()` 里那个
  `WaitForSingleObject(..., INFINITE)` 换成了 **5 s 的逾期**，拿不到锁就按「这次写失败」返回；
  同时通道名接上 `NAV2_SHM_NAMESPACE`，`add_test` 也补上了命名空间 —— 两份同名的 `test_shm.exe`
  不再抢同一把锁。外部按住互斥体 30 s 的对照：**30.75 s 不回来 → 6.92 s 自己失败退出**；
* `ros2_shim_graph_rewrite` 的读数：读者给「名字不在」和「名字在但被占」**两个预算**
  （60 ms / 1000 ms，都是上界）。同一台机器上 **20 轮里 2 轮各丢 1 个读数 → 20 轮 0 轮**；
* `slam_toolbox_mapping` 那条改掉了一半：一次被拒绝的图文件交换会**静默丢掉**那条注册，而
  nav2 / costmap 的「没人在听就不发」门控读的正是它。现在 `collect()` 走到**本进程自己**那份
  `<pid>.graph` 时改从内存里的注册表取，所以本进程自己的查询不再受那次写入成败影响 ——
  两个复现脚本（`.scratch\_partial_graph_hold.py`、`_greedy_graph_hold.py`）因此从红转绿。
  第二半是**跨进程**的，**2026-09-17 深夜也关上了**：`flush_locked()` 现在检查 `install_file()`
  的返回值，失败就把状态交给一个重刷线程（`ProcessRegistry::reflush_loop()`，10 ms 一轮，每轮
  **重新写 `.tmp` 再换**）—— 重试的必须是整次写入，因为 `ReplaceFileW` 失败时 `.tmp`
  **可能已经被带走**，「原地重试同一个 install」不成立；由一个 `dirty_` 标记驱动，所以一个
  「注册完就不再改图」的节点不会永远停在旧内容上，而第一次就成功的常见路径不花额外代价。
  回归是新增的 `ros2_shim_graph_rewrite_cross_process`（真起子进程按住文件 800 ms），
  改动、四组断言与反向验证见 `04-gaps.md` 第十六节。

以前不是这样。`ctest -j` 会随机挂，先后失败过 `shm_transport`（挂住 259 s 后被 kill）、
`ros2_shim_parameter_service`、`ros2_shim_parameter_events`、`slam_toolbox_mapping`、
`ros2_shim_graph_rewrite`，而根因是**图注册表目录没有被命名空间隔离**：通道名带上
`NAV2_SHM_NAMESPACE` 了，图目录没有，于是八条并发用例写进同一个目录、互相看得见对方的
节点和话题，凡是断言"我这条话题上有几个发布者"的用例都会跟着别人起落
（实测 `and advertises no publisher for it (1 -> 8)`）。修法是让图目录跟通道走同一个前缀
（`ros2_shim/graph.hpp` 的 `graph_dir()`）。见证据 42。

`nav2\ros2_shim` 的构建：`cmake --build nav2\ros2_shim\build`（首次要先
`cmake -S nav2\ros2_shim -B nav2\ros2_shim\build -G Ninja`，并把 UCRT64 的 `bin` 放到 PATH）。

### 14.1 编译只用 6 个大核

这台机器是 Intel Core Ultra 5 245K：6 个 P-core + 8 个 E-core，共 14 个逻辑核
（Arrow Lake 没有超线程）。要求是"编译的时候调用 6 个大核心，不要所有核心一起"，
所以三个构建入口都同时做两件事：并发任务数固定 6，并且把构建进程**钉在 P-core 上**
（Windows 子进程继承父进程的亲和性，cmake / ninja / 编译器全都留在大核上）。

```bat
:: 桥（已经默认 -j 6 + 钉核，可以加 --target 只编某个目标）
E:\venvs\py312\Scripts\python.exe bridge\scripts\build_bridge.py --target ros2_bridge_daemon

:: 兼容层 + 魔改 nav2（三个入口等价，挑一个）
E:\venvs\py312\Scripts\python.exe nav2\ros2_shim\build.py
E:\venvs\py312\Scripts\python.exe nav2\ros2_shim\build.py controller_server behavior_server
powershell -NoProfile -ExecutionPolicy Bypass -File nav2\ros2_shim\build.ps1

:: gRPC SDK
E:\venvs\py312\Scripts\python.exe gRPC\ros2_gRPC_sdk\scripts\build.py
```

`nav2\ros2_shim\build.py` 是干活的那一份，`build.ps1` 现在只是它的外壳：`-Msys2` / `-BuildType` /
`-Jobs` / `-Cores` / `-NoPin` 分别转成 `--msys2` / `--build-type` / `--jobs` 与 `BIG6_CORES` /
`BIG6_NO_PIN`，其余参数原样转给 `build.py`。所以「两个入口」不再有两份逻辑，也没有第二份工具链
路径 —— 工具链在 `tools\_toolchain.py`（Windows 上 MSYS2 UCRT64，别处用 PATH），大核规则在
`tools\_cores.py`，两个平台共用一份。

大核是实测出来的，不是照抄文档：`GetLogicalProcessorInformationEx` 报
`EfficiencyClass` 在 `{0,1,10,11,12,13}` 上是 1、在 `{2..9}` 上是 0；用定长依赖
加法链（400M 次 `volatile` 自增，延迟受限）逐核跑 3 次取最小值，前者 0.0811 s、
后者 0.0876 s，差 8 个百分点，正好是 P-core 5.2 GHz / E-core 4.6 GHz 的差距，
两边完全吻合。所以规则是"取成员数较少的那一类"（6 个 P 核 vs 8 个 E 核）。

| 想做的事 | 怎么做 |
| --- | --- |
| 手工指定大核 | `set BIG6_CORES=0,1,10,11,12,13` |
| 只限制 `-j 6`、不钉亲和性 | `set BIG6_NO_PIN=1` |
| 换并发数 | `--jobs N`（三个 python 入口）或 `-Jobs N`（`build.ps1`，同样转发成 `--jobs`） |
| 换机器（核数不同） | `BIG6_CORES=0,1,2,3,4,5`，或 `build.ps1 -Cores 0,1,2,3,4,5` |

⚠️ 历史坑，现在只对手敲 PowerShell 有效：裸词里紧跟 `=` 的变量 PowerShell **不做展开**，
`-DCMAKE_BUILD_TYPE=$BuildType` 会把字面量 `$BuildType` 塞进 cmake 缓存，`rules.ninja` 里出现
`rule CXX_SCAN__xxx_$BuildType`，ninja 报 `expected newline, got lexing error`。这个坑真的踩过：缓存
被写脏之后 `CMAKE_BUILD_TYPE:STRING=$BuildType`，只能重新 configure 回 `Release`。当年 `build.ps1`
是用双引号绕开的；现在那两行在 `build.py` 里，PowerShell 不再碰它们。
