# ros2_bridge_sim

纯 Python 的 **ROS2 模拟器 + gRPC agent**，运行在 Ubuntu 24.04 / Python 3.12 上。

只依赖 `grpcio`、`protobuf`、`numpy` 三个包：**不依赖 rclpy，也不需要安装任何 ROS2**。
它自己在进程内实现了一个最小 ROS2 图（topic / publisher / subscriber）、CDR 编解码、
一个 2D 世界与差速小车，并用 `ros2bridge.Ros2Bridge` 服务把这张图暴露到网络上。

Windows 侧的 shared-memory ROS2 shim 可以像连接真实 ROS2 一样连接它：
`ListTopics` / `GetTopicInfo` / `Subscribe` / `Publish` 的语义与线上格式完全一致。

---

## 目录结构

```
bridge/ubuntu/ros2_bridge_sim/
├── cdr.py                      CDR1 小端读写器（本工程唯一的线上格式实现）
├── messages.py                 ROS2 消息编解码（schema 表，字段顺序取自生成的 C++ 头文件）
├── graph.py                    进程内 pub/sub 图
├── world.py                    2D 世界：障碍物、OccupancyGrid 渲染、光线投射激光雷达
├── sim.py                      差速小车 SimCar（+ 简易 GoalDriver）
├── agent.py                    gRPC Ros2Bridge 服务
├── services.py                 仿真提供的服务与动作（7 个 `/sim/*` 服务 + 5 个动作）
├── run_sim.py                  CLI 入口：模拟器 + agent + 交互控制台
├── proto/                      生成的 gRPC Python 桩（已提交）
├── tests/
│   ├── test_cdr_matches_rosbags.py   与 rosbags 逐字节对比
│   ├── test_graph_agent.py           真实 gRPC 通道端到端测试
│   ├── test_services.py              服务 / 动作端到端测试
│   └── test_sim.py                   小车 / 激光雷达 / 栅格地图单元测试
├── requirements.txt
└── README.md
```

---

## 快速开始

```bash
cd bridge/ubuntu
python -m pip install -r ros2_bridge_sim/requirements.txt

# 启动模拟器（默认 0.0.0.0:50051），启动时会打印话题表
PYTHONPATH=. python -m ros2_bridge_sim.run_sim

# 指定端口 / 随机种子 / 时长，并让小车自动驶向目标点
PYTHONPATH=. python -m ros2_bridge_sim.run_sim --port 50051 --seed 7 --goal 6,-4

# 无控制台、跑 30 秒后自动退出（适合脚本 / CI）
PYTHONPATH=. python -m ros2_bridge_sim.run_sim --no-console --duration 30
```

`run_sim.py` 参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--host` | `0.0.0.0` | gRPC 绑定地址 |
| `--port` | `50051` | gRPC 绑定端口（`0` = 由系统分配） |
| `--seed` | 无 | 内部障碍物的随机种子（同种子必得同一世界） |
| `--rate` | `50.0` | 仿真步进频率 Hz |
| `--world-size METRES` | `12.0` | 方形世界的边长（原点自动居中）；边界墙贴着它内侧。默认与 `--range` 相等 —— 站在世界正中间四面墙都在半量程处、看得见，见下面「世界尺寸要跟着雷达量程走」一节 |
| `--range METRES` | `12.0` | 雷达最大量程：射线的投射上限，也是 `/scan` 里 `range_max` 的取值 |
| `--beams N` | `360` | 雷达光束数 |
| `--goal` | 无 | 形如 `X,Y` 或 `X,Y,THETA`，给定时用简易控制器把车开过去 |
| `--no-console` | 关 | 不启动交互控制台 |
| `--subscribe TOPIC[:TYPE]` | 无 | 额外订阅（可重复），每条消息按 `[sub] 话题 #序号 类型 <- 字段` 打到 stdout；给「从桥的对面推话题过来」做落点，见下 |
| `--duration` | `0` | 运行多少秒后退出，`0` 表示一直运行 |

`--subscribe` 是给桥用的测试钩子：小车只订阅 `/cmd_vel`，所以从 Windows 推一个话题过来时，
机器人这边没有落点、也没有任何证据。加上它之后：

```bash
# 机器人侧：机器人图上立刻多出这两个话题（type 可省，那时由第一条 Publish 的 type 决定）
python -m ros2_bridge_sim.run_sim --no-console \
    --subscribe /probe/feedback:nav2_msgs/msg/TrackingFeedback \
    --subscribe /probe/anything

# Windows 侧（守护进程要用重复写法加上 --uplink，逗号列表会被 cmd.exe 拆开）
ros2_bridge.bat start --uplink /cmd_vel --uplink /probe/feedback --uplink /probe/anything
ros2.bat topic pub /probe/feedback nav2_msgs/msg/TrackingFeedback current_path_index=3 --once

# 机器人侧日志
[sub] /probe/feedback #1 nav2_msgs/msg/TrackingFeedback <- {"current_path_index": 3, ...}
```

本侧没有 schema 的类型不会被丢掉，只会换成字节数（`… <- 36 byte(s), no schema for …`），
这样「字节到了但解不开」不会伪装成「什么都没发生」。

`/rosout` 是一个现成的用法 —— Windows 那边的每个节点都会把自己的日志发到这条话题上
（`nav2/ros2_shim/include/rclcpp/logging.hpp`），上行白名单里加上它，机器人侧就能读到
`level` / `msg` / `file` / `function` / `line`：

```bash
# 机器人侧
python -m ros2_bridge_sim.run_sim --no-console --subscribe /rosout
# Windows 侧
ros2_bridge.bat start --uplink /rosout
# 机器人侧日志（`line` 指向上游源码里的调用点）
[sub] /rosout #3 rcl_interfaces/msg/Log <- {"level": 30, "line": 52, "msg": "probe line 3 of 60", ...}
```

注意上行通道是「守护进程看见本机有人发这个类型的话题」之后才建的，**在那之前发的到不了**
（通道是 volatile 的）—— 实测与隐断在 `bridge/docs/03-validation.md` 证据 50。

从别的机器验证：

```python
import grpc
from ros2_bridge_sim.proto import ros2_bridge_pb2 as pb, ros2_bridge_pb2_grpc as pbg

channel = grpc.insecure_channel("192.168.1.113:50051")
stub = pbg.Ros2BridgeStub(channel)
print(stub.Ping(pb.PingRequest(payload="hi")))          # ros_distro = "sim"
print(stub.ListTopics(pb.ListTopicsRequest()))          # 话题表
for message in stub.Subscribe(pb.SubscribeRequest(topic="/scan")):
    print(message.sequence, len(message.data))          # CDR 原始字节
    break
```

---

## 交互控制台

当 `stdin` 是 tty 且未指定 `--no-console` 时，会启动一个控制台线程：

| 命令 | 说明 |
| --- | --- |
| `status` | 当前位姿、cmd_vel、仿真时间、步数、是否暂停、话题数 |
| `topics` | 打印话题表（名称 / 类型 / 发布者数 / 订阅者数） |
| `drive <linear> <angular>` | 设置前进速度 m/s 与偏航角速度 rad/s |
| `pose <x> <y> <theta>` | 直接设置小车位姿（`theta` 可省略） |
| `obstacle <x> <y> <w> <h>` | 在 `(x, y)` 处加一个 `w × h` 的矩形障碍物（中心点坐标） |
| `pause` / `resume` | 冻结 / 恢复仿真（仿真时钟同时停止） |
| `quit` | 退出 |

---

## 话题表

| 话题 | 类型 | 频率 | frame / child | 说明 |
| --- | --- | --- | --- | --- |
| `/clock` | `rosgraph_msgs/msg/Clock` | `--rate`（50 Hz） | — | 仿真的单调时钟，从 0 开始 |
| `/cmd_vel` | `geometry_msgs/msg/Twist` | 订阅 | — | 唯一输入；`linear.x` 与 `angular.z` |
| `/odom` | `nav_msgs/msg/Odometry` | `--rate`（50 Hz） | `odom` → `base_link` | 里程计（位姿 + 协方差 + 当前速度） |
| `/scan` | `sensor_msgs/msg/LaserScan` | 10 Hz | `laser` | 360 线，−π…π，0.15…12 m，无回波为 `inf` |
| `/tf` | `tf2_msgs/msg/TFMessage` | `--rate`（50 Hz） | `odom` → `base_link` | 动态变换 |
| `/tf_static` | `tf2_msgs/msg/TFMessage` | 启动时 + 每 5 s | `base_link` → `laser` | 静态变换，周期重发以便后加入的订阅者也能看到 |
| `/map` | `nav_msgs/msg/OccupancyGrid` | 1 Hz | `map` | 24 m × 24 m，0.05 m/格，原点 (−12, −12) |

世界与传感器参数：

* 世界 24 m × 24 m，分辨率 0.05 m/格（480 × 480），原点 `(-12, -12)`。
* 栅格取值：`0` = 空闲，`100` = 占用，`-1` = 未知。
* 默认世界 = 四面边界墙 + `--seed` 生成的 6 个内部障碍物（起点 2.5 m 内保持空旷）。
* 激光雷达 360 束覆盖 360°，`angle_min = -π`，`angle_increment = 2π/360`，量程 0.15…12 m。
* 仿真时钟从 0 单调递增；`agent` 上报的 `stamp_ns` 是把仿真时钟映射到墙钟
  （启动瞬间的 `time.time_ns()` + 仿真时间）。

---

## 服务与动作

除了话题，agent 同样把仿真里注册的服务与动作暴露到网络上，语义与 ROS2 一致：
Windows 侧 `ros2 service list -t` / `ros2 action list -t` 看到的就是下面这些名字。

| 服务 | 类型 | 说明 |
| --- | --- | --- |
| `/sim/pause` | `std_srvs/srv/Empty` | 冻结仿真（仿真时钟同时停住） |
| `/sim/resume` | `std_srvs/srv/Empty` | 恢复仿真 |
| `/sim/status` | `std_srvs/srv/Trigger` | 一行快照：位姿、`cmd_vel`、是否暂停 |
| `/sim/set_paused` | `std_srvs/srv/SetBool` | 唯一一个请求带字段的服务，`data=true` 等于 `pause` |
| `/sim/reset` | `std_srvs/srv/Trigger` | 停车、位姿归零、恢复运行 |
| `/sim/set_initial_pose` | `nav2_msgs/srv/SetInitialPose` | 把车瞬移到 `map` 里的某个位姿（AMCL 的 `initialpose` 形状） |
| `/sim/enable` | `nav2_msgs/srv/Toggle` | 停 / 开驱动：`enable=false` 之后小车不再响应 `/cmd_vel`，但仿真继续走 |

前五个服务的响应都是 `Trigger` 形状的 `success` + `message`（`/sim/set_paused` 也一样）。
好处是它们只用 `std_srvs`：Windows 兼容层已经生成了这几个 C++ 结构体，
两边都不用为它们发明新的消息包。后两个是**故意**用 `nav2_msgs` 的：它们存在的意义就是
让「跨机调 nav2 自己的服务类型」这件事能被真的走一遍（见 `bridge/docs/03-validation.md` 证据 24）。

⚠️ `SetInitialPose` 的位姿是**三层嵌套**（`pose.pose.pose.position.x`，
`PoseWithCovarianceStamped` 里再套 `PoseWithCovariance` 再套 `Pose`）。少读一层不会报错，
只会把小车瞬移到 `(0, 0)`。

| 动作 | 类型 | 说明 |
| --- | --- | --- |
| `/sim/drive` | `ros2_bridge_sim/action/Drive` | 沿当前朝向直行 `distance` 米，边跑边报进度，可以取消 |
| `/sim/spin` | `nav2_msgs/action/Spin` | 原地转 `target_yaw` 弧度 |
| `/sim/back_up` | `nav2_msgs/action/BackUp` | 沿 `target.x` 倒车（带符号的距离），`speed` 是速度 |
| `/sim/drive_on_heading` | `nav2_msgs/action/DriveOnHeading` | 沿当前朝向直行，带 `time_allowance` 与碰撞检查开关 |
| `/sim/wait` | `nav2_msgs/action/Wait` | 等 `time` 这么久（仿真时钟） |

四个 `nav2_msgs` 动作的存在理由是**别被车挡住**：`behavior_server` 的目标、nav2 的
`Spin` / `BackUp` 都长这样，跨机要能发才有意义。它们的 feedback / result 用的是 nav2 自己的
`schema`（`tracking_feedback.angular_distance_traveled` 之类），不是上面那套 `travelled`。
`BackUp` 与 `DriveOnHeading` 在上游**共用同一套 goal/feedback/result 布局**，所以 `target.x`
是带符号距离、`speed` 的符号必须与它一致，否则返回 `INVALID_INPUT` 而不是 abort。

`/sim/drive` 的三段 payload 只有一个量：

| 段 | 字段（`float64`） | 说明 |
| --- | --- | --- |
| goal | `distance` | 要走的距离，米；负数表示倒车 |
| feedback | `travelled` / `remaining` | 每 `0.2 s` 一条：已走 / 剩余，单位米 |
| result | `travelled` / `success` | 结束时给出：实际走了多远、是否达标 |

* 速度固定 `0.25 m/s`（`services.DRIVE_SPEED`），反馈周期 `0.2 s`（`services.DRIVE_PERIOD`）。
* 状态机与 ROS2 一致：`ACCEPTED` → `EXECUTING` →（`SUCCEEDED` / `ABORTED` / `CANCELED`）。
* **取消**：`CancelActionGoal` 置位取消标志并立刻回 `accepted=true`，goal 以 `CANCELED` 结束，
  **并且带着 `travelled`**——半途取消的 goal 也能读出它走了多远，不会让调用方白等到超时。
* **有界运行**：`2 × distance / 0.25 + 5 s` 内还没走到（车被暂停，或顶在看不见的墙上）
  就以 `ABORTED` + `travelled` 收尾，不会永远跑下去。
* 结束的 goal 在 `result_ttl_s`（默认 60 s）内仍可被 `GetActionResult` 取到，之后被回收。

---

## gRPC 行为

服务定义见 `gRPC/ros2_gRPC_sdk/proto/ros2_bridge.proto`。

| RPC | 行为 |
| --- | --- |
| `Ping` | 原样回显 `payload`，`agent_version = "ros2_bridge_sim/0.1"`，`ros_distro = "sim"` |
| `ListTopics` | 返回图中所有话题；`name_filter` 按**正则**匹配（`re.search`），非法正则返回 `INVALID_ARGUMENT` |
| `GetTopicInfo` | 按名字返回类型与发布/订阅数量；话题不存在返回 `NOT_FOUND` |
| `Subscribe` | 服务端流。客户端订阅之后发布的消息都会转发；同一话题支持任意多个并发订阅者；RPC 取消后自动清理订阅 |
| `Publish` | 接受 `encoding` 为 `"cdr"` 或空串，把字节直接送进图里，因此车上本地的 `/cmd_vel` 订阅者会收到它 |
| `ListServices` / `GetServiceInfo` | 列出图中的服务；`name_filter` 同样是正则，名字不存在返回 `NOT_FOUND` |
| `CallService` | 把请求 payload 解码后交给处理器，响应按服务的响应类型编码回传；处理器抛异常只让这一次调用失败（`success=false` + `error`），服务端继续存活 |
| `ListActions` / `GetActionInfo` | 列出动作及其 goal / feedback / result 类型 |
| `SendActionGoal` | 立刻返回 `accepted` + `goal_id`，goal 在后台线程里跑；未知动作名是 `NOT_FOUND`，被拒绝的 goal 是 `accepted=false` + 原因 |
| `ActionFeedback` | 服务端流：`accepted` / `executing` / 若干条 feedback，最后一条终止消息**同时**带 `status` 与 `result` |
| `GetActionResult` | 轮询式取结果；`timeout_ms` 内没结束就回 `success=false` + `"goal is still running"` |
| `CancelActionGoal` | 置位取消标志、置 `CANCELING`；已经结束的 goal 回 `accepted=false` |

`Publish` 的失败路径全部返回 `accepted=false` + 说明，**不会让服务端崩溃**：

* 不支持的 `encoding`（例如 `json`）；
* 空 payload；
* 无法解码的 payload（截断、只有 4 字节头、非法的 encapsulation header 等）；
* 话题类型未知且请求里也没给 `type`。

对**没有 schema 的类型**（本工程没有内置的消息），payload 会被原样转发而不是拒绝——
这样 Windows 侧可以自己定义消息类型。

`Subscribe` 的 `throttle_ms > 0` 时，比该周期更快的消息会被丢弃（按服务端墙钟计时）。
慢订阅者最多落后 `DEFAULT_QUEUE_SIZE`（1024）条消息，超出后丢弃最旧的，避免服务端内存无界增长。

---

## CDR 契约

线上格式是真实 ROS2 的 DDS-CDR（CDR1，小端），`data` 字段包含 4 字节 encapsulation header
`00 01 00 00`。规则（`cdr.py` 完整实现）：

* **对齐基准是 header 之后的第一个字节**，即 body 的起始位置（Fast-CDR 的
  `Cdr::reset_alignment()` 行为）。
* 标量大小 / 对齐：`bool`/`byte`/`char`/`uint8`/`int8` = 1，`int16`/`uint16` = 2，
  `int32`/`uint32`/`float32` = 4，`int64`/`uint64`/`float64` = 8。
* `string`：4 字节对齐的 u32 长度（**含**结尾 NUL），随后是 UTF-8 字节，最后是 `\0`。
* `sequence<T>`：4 字节对齐的 u32 元素个数，随后是元素。
* 定长数组 `T[N]`：没有个数前缀，元素紧紧相邻、各自按自身要求对齐。
* 嵌套消息：按声明顺序排列其字段，没有额外的长度或对齐前缀。
* **CDR1 不会为嵌套结构体插入额外的前置填充**：结构体成员的对齐要求等于它第一个字段的对齐要求。
  这一点与 `rosbags` 的实现一致（`align(NAME) == align(fields[0])`），
  并有逐字节测试兜底（见下）。

`messages.py` 里字段顺序与类型**完全来自生成的 C++ 头文件**
（`nav2/ros2_shim/generated/include/<pkg>/msg/*.hpp`）：例如 ROS2 的 `Header` 只有
`{Time stamp, string frame_id}`，没有 `seq`。消息在 Python 里就是普通字典：

```python
{"header": {"stamp": {"sec": 1, "nanosec": 0}, "frame_id": "odom"}, "child_frame_id": "base_link", ...}
```

已实现的消息类型：`Time`、`Header`、`String`、`Vector3`、`Point`、`Quaternion`、`Pose`、
`PoseWithCovariance`、`Twist`、`TwistWithCovariance`、`PoseStamped`、
`PoseWithCovarianceStamped`、`Transform`、`TransformStamped`、`MapMetaData`、
`OccupancyGrid`、`Odometry`、`Path`、`LaserScan`、`TFMessage`、`Clock`、
`Log`（`rcl_interfaces/msg/Log`，桥对面的 `/rosout`）。

### 世界尺寸要跟着雷达量程走

激光雷达看**不到**量程之外的东西，而边界墙就贴着世界的边。这个默认值因此跟 `--range` 走：
现在是 `12.0`，站在正中间离四面墙约 5.8 m，360 束**全部**有回波。旧默认是 `24.0` —— 站在正
中间离四面墙 11.8 m，正好卡在 12 m 量程的边缘，360 束里只有沿四条轴线的那几束勉强有回波，
其余全是 `inf`。对 SLAM 这类要把扫描放进地图的消费者来说，那样的世界等于「没有墙」：仿真器
自己的 `/map`（世界真值）是完整的，建出来的图却是空的。

```bash
# 默认：12 m 世界，墙在 ±5.8 m，360 束全部有回波（2.9 m .. 8.2 m）
python -m ros2_bridge_sim.run_sim --no-console

# 想反过来验证「世界太大」这个前提，就加大光束数/量程
python -m ros2_bridge_sim.run_sim --no-console --world-size 24 --range 20 --beams 720
```

世界尺寸是**必要**条件，不是**充分**条件：`slam_toolbox` 那三个运动门槛
（`minimum_travel_distance` / `minimum_travel_heading` / `minimum_time_interval`）要是按真车的
值配，慢速仿真车几乎不会被采样，图里同样只剩车周围一小片、占据格子是 0。两个都改到位之后，
一趟 4.5 m 的走位就能建出整间房 —— 见 `code/Desktop/racecar/src/racecar/config/slam_toolbox.yaml`
文件头的「运动门槛」注解，以及 `bridge/docs/03-validation.md` 证据 66。

`SimCar` 把 `--range` 同时写进 `/scan` 的 `range_max` 字段，所以消息报的量程永远等于射线
真正用的量程；这两个数以前是写死的 `12.0` 字面量。

---

## 测试

```bash
cd bridge/ubuntu
export PYTHONPATH=$PWD

# 全部测试
python -m pytest ros2_bridge_sim/tests -v

# 逐字节与 rosbags 对比（需要 rosbags；未安装时会干净地 skip）
python ros2_bridge_sim/tests/test_cdr_matches_rosbags.py

# 单元测试与端到端测试也可以直接当脚本跑
python ros2_bridge_sim/tests/test_sim.py
python ros2_bridge_sim/tests/test_graph_agent.py
python ros2_bridge_sim/tests/test_services.py
python ros2_bridge_sim/tests/test_watch.py
```

* `test_cdr_matches_rosbags.py` 从仓库里的 `.msg` 文件（`nav2/third_party/**` 与
  `nav2/nav2_extracted/navigation2-1.5.1/nav2_msgs/**`）构建 `rosbags` typestore，
  用 `rosbags` 序列化 LaserScan / Odometry / TFMessage / OccupancyGrid / Twist / Path，
  断言本工程的编码器产出**完全相同的字节**，并断言解码器能往返 `rosbags` 的字节。
  `rosbags` 未安装或找不到仓库时，会用清晰的信息跳过（可用 `ROS2_BRIDGE_REPO` 指定仓库根目录）。
* `test_graph_agent.py` 在临时端口上真正启动 agent，用真实 `grpc` 客户端验证
  `ListTopics`（含 `/scan` `/odom` `/map` `/tf`）、`Subscribe`（5 秒内至少 3 条、`encoding == "cdr"`）、
  通过 `Publish` 下发 `/cmd_vel` 让小车动起来、以及不支持编码被拒绝后服务端仍然存活。
* `test_services.py` 用真实 `grpc` 客户端验证全部服务与动作 RPC：
  `ListServices` / `GetServiceInfo` / `CallService`（Trigger 与空请求往返、`SetBool` 请求字段、
  处理器抛异常后服务端仍存活），以及 `SendActionGoal` → `ActionFeedback` 流 → `GetActionResult`；
  还包括取消（`CANCELED` 带 `travelled`）、尚在跑的 goal 轮询不会误报、
  未知 goal / action 的报错、以及 TTL 过后的回收。
  四个 `nav2_msgs` 动作各有断言：`/sim/spin` 转够 1.0 rad、`/sim/back_up` 倒够 0.4 m、
  符号不一致的 `BackUp` goal 回 `INVALID_INPUT`（713）、`/sim/wait` 的剩余时间在递减；
  `test_list_actions_shows_every_action_the_sim_serves` 断言**动作表按名字排序**（不是注册顺序）。
* `test_sim.py` 验证 `cmd_vel` 积分（直线 / 原地转 / 圆弧）、暂停与恢复、激光雷达对着墙比空旷处更近、
  以及栅格地图把障碍物所在格标成 `100`。

---

## 重新生成 gRPC 桩

```bash
cd <repo root>
python -m grpc_tools.protoc \
  -I gRPC/ros2_gRPC_sdk/proto \
  --python_out=bridge/ubuntu/ros2_bridge_sim/proto \
  --grpc_python_out=bridge/ubuntu/ros2_bridge_sim/proto \
  gRPC/ros2_gRPC_sdk/proto/ros2_bridge.proto
```

生成后唯一需要的手工修改：把 `ros2_bridge_pb2_grpc.py` 里的
`import ros2_bridge_pb2 as ros2__bridge__pb2` 改成相对导入
`from . import ros2_bridge_pb2 as ros2__bridge__pb2`，
这样 `from ros2_bridge_sim.proto import ros2_bridge_pb2` 在任何工作目录下都能用。
