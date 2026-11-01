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
  <li><a href="#markers">Marker（箭头、线、点）</a></li>
  <li><a href="#costmap">代价地图与点云</a></li>
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
| **看 Marker** | `visualization_msgs/msg/Marker` 与 `MarkerArray`：箭头、线段、点、方块（rviz 那套 marker 类型） |
| **看代价地图** | `nav2_msgs/msg/Costmap` 与 `CostmapUpdate`：按调色板叠加在地图上，支持增量更新 |
| **看点云** | `sensor_msgs/msg/PointCloud2`：按高度上色，也可以压平成一个颜色 |
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

工具栏最左边两个**带名字的按钮**就是两个工具（和 rviz 的工具条一个位置，
名字也用 rviz 的那两个）：`2D Goal Pose` 与 `2D Pose Estimate`。选中的那个会高亮。

| 操作 | 结果 |
| --- | --- |
| 点工具栏「2D Goal Pose」按钮，再点视口 | 发 `/goal_pose`（`geometry_msgs/msg/PoseStamped`） |
| 拖动代替点击 | 朝向 = 拖动方向；拖动距离小于 15 cm 时朝向为 0 |
| 点工具栏「2D Pose Estimate」按钮，再点视口 | 发 `/initialpose`（`geometry_msgs/msg/PoseWithCovarianceStamped`） |
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
    "view": { "fixed_frame": "map", "grid_cell_m": 1.0, "zoom": { "min": 0.02, "max": 80 } }
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
    "laser_scan": { "color": "#f44336", "size": 6 },
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

<a id="markers"></a>

## 3.5.6 Marker（箭头、线、点）

`visualization_msgs/msg/Marker` 与 `MarkerArray` 的话题可以直接加进面板，
画出来的是 rviz 那套东西：

| marker 类型 | 画成什么 |
| --- | --- |
| `ARROW` (0) | 箭头：杆身 + 三角头，方向是 marker 的朝向，长度是 `scale.x` |
| `ARROW_STRIP` (12) | 一串箭头（每两个点一个） |
| `LINE_STRIP` (4) / `LINE_LIST` (5) | 折线 / 一段段独立的线 |
| `POINTS` (8) | 点，大小是 `scale.x` |
| `CUBE` (1) / `SPHERE` (2) / `CYLINDER` (3) | 位置上的一个方块（2D 视图里球和柱都是方块） |
| `CUBE_LIST` (6) / `SPHERE_LIST` (7) | 一串方块 |
| `TRIANGLE_LIST` (11) | 三角形 |

几个要点：

* **颜色用 marker 自带的**（`color.r/g/b/a`，0..1）；alpha 为 0 时画成品红，
  免得完全看不见。
* **同一个话题可以有很多 marker**：按 `ns` + `id` 分开存，后来的覆盖先来的。
* **删除是认的**：`action = 2`（DELETE）删掉那一个，`action = 3`（DELETEALL）
  清空这个话题。
* **坐标变换在边车里做**：marker 的 `header.frame_id` 会先变换到视口的固定坐标系，
  marker 自带的 `points`（线段、点集）也跟着变换。
* 一个 `MarkerArray` 里的每个 marker 可以有自己的坐标系，边车逐个查 TF。

加法：面板里「Add a topic from the graph」选到那个话题 → `Add`。
面板的显示属性里可以改颜色；marker 自带颜色时以自带的为准。

<a id="costmap"></a>

## 3.5.6b 代价地图与点云

### 代价地图

支持三种话题，画的都不是另起一层，而是**叠在地图上**：

| 话题类型 | 说明 |
| --- | --- |
| `nav2_msgs/msg/Costmap` | nav2 自己的代价地图，带 `layer` 名（master / global / local） |
| `nav2_msgs/msg/CostmapUpdate` | 增量更新：只改一个矩形区域 |
| `map_msgs/msg/OccupancyGridUpdate` | 占据栅格的增量更新（`/map_updates`） |

调色板跟 nav2 的 Costmap Display 一致，代价 0 是空的（**透明**，底下的地图能透出来）：

| 代价 | 画成什么 | 默认颜色 |
| --- | --- | --- |
| `0` | 自由空间，不画 | 透明 |
| `1` .. `100` | 从低代价色渐变到高代价色 | `#2b6cff` → `#ff4040` |
| `253` | 未知 | `#7a7a7a` |
| `254` | 内切障碍（inscribed） | `#00e5ff` |
| `255` | 致命障碍（lethal） | `#101010` |

增量更新会**合并进已有的网格**，然后让纹理失效重画 —— 所以局部代价地图
（`/local_costmap/costmap_updates`）和全局代价地图可以同时开着，
各自更新各自的那块区域，不会互相覆盖。

### 点云

`sensor_msgs/msg/PointCloud2` 支持 `x` / `y` / `z` / `intensity` 四个字段：

* **高度上色**（默认）：把 `z` 从低到高映射成蓝→绿的彩虹，
  于是点云看起来像一张地形图而不是一团点；只有 `x`/`y` 时就退化成单色。
* **点大小**、**最大距离**（超出半径的点不画）、**透明度**都可以调。
* 点云在传感器自己的坐标系里时，边车会**先查 TF 变换到视口的固定坐标系**
  再画 —— 和其它图层一个规矩。
* `point_step`、`row_step`、大小端都按消息里的来；`NaN` 点自动跳过。

配置示例：

```json
{
  "topics": {
    "/local_costmap/costmap": {
      "type": "nav2_msgs/msg/Costmap",
      "qos": { "reliability": "reliable", "durability": "transient_local", "depth": 1 }
    },
    "/lidar/points": {
      "_comment": "深度相机是 best_effort",
      "type": "sensor_msgs/msg/PointCloud2",
      "qos": { "reliability": "best_effort", "depth": 5 }
    }
  },
  "display": {
    "costmap": {
      "low_color": "#2b6cff", "high_color": "#ff4040",
      "inscribed_color": "#00e5ff", "lethal_color": "#101010",
      "unknown_color": "#7a7a7a", "alpha": 0.75
    },
    "point_cloud": {
      "color": "#00d0ff", "size": 2,
      "color_by_height": true, "max_range": 20
    }
  }
}
```

<div class="callout note">
<span class="label">点云的 QoS 有个坑</span>
<p>点云的发布端可能是 best_effort（深度相机）也可能是 reliable
（<code>ros2 topic pub</code>、bag 回放）。**发布端的 QoS 决定谁收得到**：
best_effort 的发布端只配 best_effort 的订阅，reliable 的发布端只配 reliable 的订阅。
所以 agent 对点云<strong>订阅两次</strong>（一次 reliable、一次 best_effort），
两种发布端都能收到，而且一条消息不会被收两遍。</p>
</div>

<a id="qos"></a>

## 3.5.7 QoS

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

## 3.5.8 验证到哪一步了

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
* 真点工具栏「2D Goal Pose」/「2D Pose Estimate」→ 点视口 → `/goal_pose` 与 `/initialpose` 都被独立的
  `ros2 topic echo` 收到（frame_id、x/y、四元数都对）
* 真 ROS2（Jazzy）对端：best_effort 的 `/qos_probe/scan` 与 transient_local 的
  `/qos_probe/map` 都收到；把 sensor 改回 reliable 的负对照里扫描一条也收不到

* Marker：本地发的 `Marker` 与**真 ROS2 对端发的 `MarkerArray`** 都收到
  （两个 marker 的 ns/id、位置、颜色都对），真窗口截图里箭头是 2144 个品红像素

* 代价地图：真 ROS2 对端发的 `nav2_msgs/msg/Costmap` 收到
  （`resolution` 0.5、3×2 网格、`origin` (1,2)、`layer` master、6 个代价值都对）；
  增量更新合并进已有网格并让纹理失效
* 点云：真 ROS2 对端发的 `PointCloud2` 收到（4 个点、z 从 0 到 1.5、包络
  x 1..7 / y 2..8 都对）；best_effort 与 reliable 两种发布端**都**收到了
  （一个传感器一个 `ros2 topic pub`），这是 agent 双订阅的效果
* 渲染：代价地图的调色板颜色（高代价红 / 内切青 / 致命深色）与点云的高度
  彩虹（低点蓝 / 高点红）都在渲染出的图像里数到了像素

边车侧（`viz_feed --selftest`，也是 ctest）：`map -> laser (x=10)` + laser 系位姿
`(1,2)` → 边车输出 map 系 `(11,2)`；地图 int8 数据 base64 解出 `[0,100,-1,0]`；
`set_fixed_frame` 后 latched 地图从 `(5,6)` 立刻重发成 `(-5,6)`。

<a id="limits"></a>

## 3.5.9 还没做的

| 缺口 | 现状 |
| --- | --- |
| 3D 与 URDF、TF 坐标轴 | 没有；`nav2_rviz_plugins` 仍然没移植，点云只有 2D 俯视 |
| 点云的 per-point RGB / 法线 | 只读 x / y / z / intensity；带 RGB 字段的点云按高度或单色画 |
| 代价地图的分层叠加（costmap layer 组合） | 每个话题单独一层，按加入顺序叠，没有 nav2 那种「多层合成一个 master」 |
| action 发目标 | v1 只发 `/goal_pose` 话题，不走 `NavigateToPose` action（没有进度反馈与中途取消） |
| 逐话题 QoS 覆盖 | proto 里预留了字段，daemon 还没透传 |
| 显示属性的图形化编辑 | 目前是面板里的文本框（颜色 / 大小 / 透明度），没有取色器 |

代码与协议在仓库的 `viz/` 目录里，细节见那里的
[README](https://github.com/XiaoYuyu520520/mini-ros2/blob/main/viz/README.md)。
