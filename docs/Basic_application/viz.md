---
layout: default
title: 3 基础应用开发 · Windows 可视化
---

# 3.5 Windows 可视化（rviz2 的替代）

<div class="overview">
<div class="label">本页总览</div>
<p>这一页讲 <code>viz.exe</code>：一个在 Windows 上看共享内存里的 ROS2 图的 2D 可视化工具。
它替代的是 rviz2 的「看得见」与「点目标」两件事，<strong>不是</strong> rviz2 的移植：
没有 Qt、没有 OGRE、没有插件体系。</p>
<ul>
  <li><a href="#what">它能做什么</a></li>
  <li><a href="#run">怎么跑起来</a></li>
  <li><a href="#tools">发目标与初始位姿</a></li>
  <li><a href="#config">配置文件</a></li>
  <li><a href="#sidecar">边车与 TF</a></li>
  <li><a href="#qos">QoS</a></li>
  <li><a href="#evidence">验证到哪一步了</a></li>
  <li><a href="#limits">还没做的</a></li>
</ul>
</div>

<a id="what"></a>

## 3.5.1 它能做什么

| 能力 | 说明 |
| --- | --- |
| 看 `/map` | 占据栅格按 free / occupied / unknown 三色画，原点与朝向按消息里的位姿摆 |
| 看 `/scan` | 雷达回波点，颜色与点大小可调；位置已经在**固定坐标系**里 |
| 看 `/plan` | 规划出来的路径折线，带每点的朝向 |
| 看机器人位姿 | `/amcl_pose` / `/odom` 的位置、朝向与位置协方差椭圆 |
| 自由缩放平移 | 滚轮以指针为锚点缩放，左键拖动平移，网格按米自适应 |
| **发目标点** | 2D Goal Pose：点一下是位置，拖一下是朝向，发 `/goal_pose` |
| **设初始位姿** | 2D Pose Estimate：发 `/initialpose`，协方差用 rviz 的 0.25 / 0.25 / 0.0685 |
| 加话题 | 面板里从图里选一个话题加进来，不用改代码、不用重启 |
| 显示属性 | 逐话题覆盖颜色 / 点大小 / 透明度；空着就用该类型的默认样式 |
| 配置保存 / 读取 | 工具栏两个按钮，**保留文件里的中文注释**（JSON 没有注释语法，注释写成 `_comment` 键） |

坐标系的规矩：**工具发的是它自己的 frame**（配置里 `tools.*.frame`，默认 `map`），
不是视口当前看的固定坐标系。目标点属于地图，哪怕你正在看 odom 里的机器人。

<a id="run"></a>

## 3.5.2 怎么跑起来

### [Windows]

加载环境之后，命令名就是 `rviz2`：

```bat
:: 加载共享内存环境（一次就够，这个窗口里一直有效）
call bridge\windows\bridge_env.bat

:: 启动可视化
rviz2
```

`rviz2.bat` 会找到 `viz\viz.exe` 并把它拉起来；`viz.exe` 再把数据面
`viz_feed.exe` 作为子进程拉起来（见 <a href="#sidecar">边车</a>）。

<div class="callout note">
<span class="label">namespace 必须一致</span>
<p>边车和 nav2 必须在<strong>同一个</strong> <code>NAV2_SHM_NAMESPACE</code> 里，否则边车
看不到任何话题。<code>bridge_env.bat</code> 已经把这个值设给整个窗口，
<code>viz.exe</code> 会把它传给子进程；配置文件里的 <code>transport.namespace</code>
可以覆盖它。</p>
</div>

### 自己编译

```powershell
cd viz
$env:PATH = "D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin;$env:PATH"
go build -ldflags "-H windowsgui" -o viz.exe .
```

数据面在另一个树里：

```powershell
python bridge\scripts\build_bridge.py --jobs 1
```

`viz.exe` 会在自己旁边找 `viz_feed.exe`，找不到就去仓库的
`bridge\windows\build` 找；也可以用配置里的 `transport.cmd` 指到别处。

<a id="tools"></a>

## 3.5.3 发目标与初始位姿

工具栏最左边两个图标就是两个工具（和 rviz 的工具条一个位置）：

| 操作 | 结果 |
| --- | --- |
| 点工具栏「目标」图标，再点视口 | 发 `/goal_pose`（`geometry_msgs/msg/PoseStamped`） |
| 拖动代替点击 | 朝向 = 拖动方向；拖动距离小于 15 cm 时朝向为 0 |
| 点工具栏「初始位姿」图标，再点视口 | 发 `/initialpose`（`geometry_msgs/msg/PoseWithCovarianceStamped`） |
| **Esc** | 取消当前工具 |

<div class="callout warn">
<span class="label">一个通道只能有一个写者</span>
<p>共享内存的通道是<strong>单写者</strong>的。边车自己持有 <code>/goal_pose</code> 与
<code>/initialpose</code> 的写端（启动时就建好并登记进图），所以这两个话题<strong>由
viewer 发</strong>：不要再对同一通道跑 <code>ros2 topic pub</code>，否则两个写者会
互相截断对方的环形缓冲。要在命令行发，就先把 viewer 关掉，或者把配置里的工具话题
改成别的名字。</p>
</div>

<a id="config"></a>

## 3.5.4 配置文件

`viz/config.json`（可执行文件旁边）。所有键都是可选的，缺的用默认值补；
**JSON 没有注释语法**，注释写成以 `_comment` 开头的键，程序会忽略它。

```json
{
  "_comment": "这一行是注释",
  "window": { "width": 1440, "height": 900 },
  "layout": { "panel_offset": 0.74 },
  "views": {
    "view": { "fixed_frame": "map", "grid_cell_m": 1.0, "zoom": { "min": 0.02, "max": 40 } }
  },
  "topics": {
    "/map": {
      "type": "nav_msgs/msg/OccupancyGrid",
      "qos": { "reliability": "reliable", "durability": "transient_local", "depth": 1 }
    },
    "/scan": {
      "_comment": "真雷达是 best_effort，写成 reliable 一条也收不到",
      "type": "sensor_msgs/msg/LaserScan",
      "qos": { "reliability": "best_effort", "depth": 5 }
    }
  },
  "display": {
    "laser_scan": { "color": "#00ff88", "size": 2.5 },
    "path": { "color": "#ff4444", "width": 2.5 },
    "pose": { "color": "#ffcc00", "size": 14, "covariance": true }
  },
  "tools": {
    "goal_topic": { "topic": "/goal_pose", "type": "geometry_msgs/msg/PoseStamped", "frame": "map" },
    "initial_pose": { "topic": "/initialpose", "type": "geometry_msgs/msg/PoseWithCovarianceStamped", "frame": "map" }
  },
  "transport": { "cmd": "", "namespace": "" }
}
```

几条规矩：

* **「缺失才补默认」是按节算的**：文件里写了 `topics`，就只用文件里的那些，
  不会偷偷把默认的 `/map` `/scan` `/plan` 加回来；完全没写 `topics` 才用默认表。
* **保存写的是「屏幕上现在是什么」**：先拖大窗口再按 Ctrl+S，新尺寸会被记住；
  收起状态的面板存的是它**展开时**的宽度。
* **保存不会删注释**：新配置是**深合并**进原文档的，只覆盖它提到的键。
* 带 UTF-8 BOM 的文件（Windows 编辑器常见）也能读。

<a id="sidecar"></a>

## 3.5.5 边车与 TF

数据面是 `bridge/windows/tools/viz_feed.cpp` 编出来的 `viz_feed.exe`。它**不是**
一个转发器，而是一个普通的 shim 节点：

* 它是 `rclcpp::Node`，订阅会登记进图 —— nav2 因此知道有人在看 `/plan`，
  才会把计划发出来；
* 它带 `tf2_ros::Buffer` 与 `TransformListener`，**每条消息在边车里就变换到视口的
  固定坐标系**，GUI 拿到的永远是固定系里的几何结果；
* 固定坐标系一变（`set_fixed_frame`），它**立刻重发**每个话题的最后一帧，
  所以 latched 地图不用等下一次发布；
* 协议是长度前缀的二进制帧走 stdin/stdout：4 字节小端长度 + 1 字节类型 + 负载。
  stdout 只走帧，日志走 stderr。

`/goal_pose` 与 `/initialpose` 的写端也归它（见上面的单写者提醒）。

<a id="qos"></a>

## 3.5.6 QoS

共享内存那一层**没有** QoS：可靠性与持久性只在 **DDS 边界**（Ubuntu 侧的 agent）
决定能不能匹配。所以：

| 话题 | 发布端实际 QoS | agent 请求的 QoS | 结果 |
| --- | --- | --- | --- |
| `/scan`（真雷达） | best_effort | **reliable**（旧行为） | **不匹配，一条也收不到** |
| `/map`、`/tf_static` | transient_local | **volatile**（旧行为） | 晚订阅就错过 latch 的那一条 |

现在 agent 侧按 rviz 的默认表选 QoS：

| 规则 | QoS |
| --- | --- |
| `sensor_msgs` 的激光 / 点云 / 图像 / IMU | best_effort, volatile, depth 5 |
| `/map`、`/map_updates`、`/tf_static`、`/robot_description`、代价地图 | reliable, **transient_local**, depth 1 |
| `/clock` | best_effort, volatile, depth 1 |
| 其它 | reliable, volatile, depth 10 |

逐话题覆盖的 `QosProfile` 已经在 proto 里**预留**（`SubscribeRequest.qos` /
`PublishRequest.qos`），但 daemon 还没透传，所以 GUI 配置里的 `qos` 目前只影响
本地「读 latch 的那一条」这一层。

这张表不是纸面推演：2026-10-31 在 `wang@192.168.0.153` 的 **ROS2 Jazzy** 上量过。
对端用 `bridge/scripts/qos_probe.py` 发两路：

```bash
# Ubuntu（先 source /opt/ros/jazzy/setup.bash）
python3 bridge/scripts/qos_probe.py
#  /qos_probe/scan  best_effort 10 Hz
#  /qos_probe/map   transient_local 0.5 m / 格
```

```bat
:: Windows
set ROS2_BRIDGE_AGENT=192.168.0.153:50111
ros2 topic echo /qos_probe/scan --count 3 --timeout 15
ros2 topic echo /qos_probe/map --once --timeout 15
```

对端的 rclcpp agent 就是被验的那一档：源码在
`gRPC/ros2_gRPC_sdk/src/server/rclcpp_backend.cpp` 的 `qosFor()`，
改完在 Ubuntu 上 `cmake --build gRPC/ros2_gRPC_sdk/build/linux-x86_64 --target ros2_grpc_sdk_agent`
再重启 agent 即可。

修好之后两条都收到（扫描 3 条、`ranges` 21 个值；地图 400 格 / 76 个占用）。
**负对照**：把 sensor 分支临时改回 reliable，agent 立刻报
`offering incompatible QoS ... RELIABILITY_QOS_POLICY`，Windows 端的扫描
**0 字节**，而地图照常收到；改回 best_effort 后扫描马上恢复。详细记录见
[`bridge/docs/03-validation.md`](https://github.com/XiaoYuyu520520/mini-ros2/blob/main/bridge/docs/03-validation.md)
第三节。

<a id="evidence"></a>

## 3.5.7 验证到哪一步了

已验证（`go test`，不需要显示器）：

* 帧格式往返、超长帧被拒、真边车的 hello / topics（用独立 namespace）
* 地图纹理的调色板、协方差椭圆的半径、地图画在原点的位置、折线落在中线
* 工具：点击给位置、拖拽给朝向（向上拖 = +90°）、再点一次取消、无工具时拖拽仍然平移
* 配置：注释保留、BOM 容错、保存写当前布局、读取应用布局

已验证（真窗口 + 真共享内存，端到端）：

* `viz.exe` 启动后自己拉起 `viz_feed.exe`；窗口关闭后子进程一起退出（残留 0 个）
* 发 `/tf_static`（`map -> laser`）、`/map`（20×20 占用栅格）、`/scan`、`/plan`、
  `/amcl_pose` 之后，窗口里出现：地图占用边框 32861 个像素、扫描点 27 个、
  路径 520 个、机器人位姿 6316 个（按各自的颜色数出来的）
* 真点工具栏图标 → 点视口 → `/goal_pose` 与 `/initialpose` 都被独立的
  `ros2 topic echo` 收到（frame_id、x/y、四元数都对）
* 真 ROS2（Jazzy）对端：best_effort 的 `/qos_probe/scan` 与 transient_local 的
  `/qos_probe/map` 都收到；把 sensor 改回 reliable 的负对照里扫描一条也收不到

边车侧（`viz_feed --selftest`，也是 ctest）：`map -> laser (x=10)` + laser 系位姿
`(1,2)` → 边车输出 map 系 `(11,2)`；地图 int8 数据 base64 解出 `[0,100,-1,0]`；
`set_fixed_frame` 后 latched 地图从 `(5,6)` 立刻重发成 `(-5,6)`。

<a id="limits"></a>

## 3.5.8 还没做的

| 缺口 | 现状 |
| --- | --- |
| 代价地图叠加、点云、Marker | 只有地图 / 扫描 / 路径 / 位姿四类图层 |
| 3D 与 URDF | 没有；`nav2_rviz_plugins` 仍然没移植 |
| action 发目标 | v1 只发 `/goal_pose` 话题，不走 `NavigateToPose` action（没有进度反馈与中途取消） |
| 逐话题 QoS 覆盖 | proto 里预留了字段，daemon 还没透传 |
| 显示属性的图形化编辑 | 目前是面板里的文本框（颜色 / 大小 / 透明度），没有取色器 |

代码与协议在仓库的 `viz/` 目录里，细节见那里的
[README](https://github.com/XiaoYuyu520520/mini-ros2/blob/main/viz/README.md)。
