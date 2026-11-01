# viz — Windows 上的共享内存 ROS2 可视化

这是 rviz2 的替代品：一个能拖动、能缩放、能收起面板的 2D 视口，看共享内存里
的 `/map`、`/scan`、`/plan`、机器人位姿和 **Marker（箭头、线段、点、方块）**，
并且能**发目标点**和**设置初始位姿**。

数据面是 C++ 边车 `viz_feed.exe`（在 `bridge/windows` 里编译）：它是一个普通
的 shim 节点，用 `tf2` 把每条消息变换到视口的固定坐标系，再把结果通过
stdin/stdout 交给这个 Go 界面。`viz.exe` 启动时自己把它拉起来。

```
go run .                       # 读可执行文件旁边或当前目录的 config.json
go run . -config other.json
```

边车在 `viz.exe` 旁边找不到时，会去仓库的 `bridge/windows/build` 找；也可以用
`transport.cmd` 指定。**边车和 nav2 必须在同一个 `NAV2_SHM_NAMESPACE` 里**。

## 构建（Windows）

cgo 需要一个 C 编译器，用 MSYS2 UCRT64 的 gcc：

```powershell
$env:PATH = "D:\WindowsServer2025_1\software\MSYS2\ucrt64\bin;$env:PATH"
go build -ldflags "-H windowsgui" -o viz.exe .
```

`-H windowsgui` 去掉控制台窗口。想看到日志就先不要这个参数。

Linux 上同样的代码，`go build -o viz .` 即可（需要 `libgl1-mesa-dev` 等
Fyne 的系统依赖）。

## 现在的行为

| 操作 | 结果 |
| --- | --- |
| 拖动视口（左键按住拖） | 平移 |
| 滚轮 | 以指针为锚点缩放（指针下的世界点不动） |
| 拖窗口边框 | 视口重新栅格化，网格跟着重建 |
| 工具栏 ↻ / **F8** | 回到原点、40 px/m |
| 工具栏 + | 往话题列表加一条（验证列表机制） |
| 工具栏 ☰ / **F9** | 收起/展开右侧面板，展开时回到原来的宽度 |
| 右侧手风琴标题 | 单独收起某一节 |
| 状态栏 | 光标世界坐标、缩放、**当前 DPI 缩放** |
| 工具栏「打开」 / **Ctrl+O** / **F5** | 重新读配置文件（窗口大小、分隔条、话题表都跟着变） |
| 工具栏「保存」 / **Ctrl+S** | 把**当前**状态写回配置文件（含窗口当前尺寸和分隔条位置） |
| 工具栏「2D Goal Pose」+ 点击/拖拽 | 发 `/goal_pose`：位置=按下点，朝向=拖动方向（拖动 < 15 cm 时朝向为 0） |
| 工具栏「2D Pose Estimate」+ 点击/拖拽 | 发 `/initialpose`，协方差用 rviz 的 0.25 / 0.25 / 0.0685 |
| 加 Marker 话题 | 话题列表里 `Add` 一个 `visualization_msgs/msg/Marker` 或 `MarkerArray`；箭头/线/点/方块按 marker 的类型和颜色画 |
| **Esc** | 取消当前工具 |
| 启动后 | 自动拉起 `viz_feed.exe`；状态栏显示边车状态、固定坐标系、每个话题的 Hz |

保存写的是「屏幕上现在是什么」，不是「上次读进来的文件」：先拖大窗口再按
Ctrl+S，新尺寸会被记住。收起状态的面板不会被保存成布局——存的是它**展开时**
的宽度，否则下次启动面板会贴在边上。

**发点的坐标系**：工具发的是**工具自己的** frame（配置里 `tools.*.frame`，
默认 `map`），不是视口当前看的固定坐标系。目标点属于地图，哪怕你正在看
odom 里的机器人。

**DPI**：Fyne 自带 per-monitor DPI 感知（实测进程 `dpiAware=2`）。视口的位图
按「逻辑尺寸 × 画布缩放」生成再铺满控件，所以高 DPI 下网格是清晰的，不是被
主题放大糊掉。`FYNE_SCALE=2` 是手动覆盖，用来验证缩放路径。

## 配置

`config.json` 里所有键都是可选的，缺的用默认值补。**JSON 没有注释语法**，
所以注释写成以 `_comment` 开头的键——`encoding/json` 会忽略不认识的键。

「缺失才补默认」是按**节**算的：文件里写了 `topics`，就只用文件里的那些，
不会偷偷把默认的 `/map` `/scan` `/plan` 加回来；完全没写 `topics` 才用默认表。
带 UTF-8 BOM 的文件（Windows 编辑器常见）也能读。

```json
{
  "_comment": "这一行是注释",
  "window": { "width": 1440, "height": 900 },
  "layout": { "panel_offset": 0.74 },
  "views": {
    "view": {
      "fixed_frame": "map",
      "grid_cell_m": 1.0,
      "zoom": { "min": 0.02, "max": 80 }
    }
  },
  "topics": {
    "/scan": {
      "type": "sensor_msgs/msg/LaserScan",
      "qos": { "reliability": "best_effort", "depth": 5 },
      "color": "#00ff88"
    }
  },
  "display": {
    "_comment": "按类型的默认样式；话题里的 color/size/alpha 是逐话题覆盖",
    "occupancy_grid": { "free_color": "#f2f2f2", "occupied_color": "#202020",
      "unknown_color": "#7a7a7a", "alpha": 0.85 },
    "laser_scan": { "color": "#00ff88", "size": 2.5 },
    "path": { "color": "#ff4444", "width": 2.5 },
    "pose": { "color": "#ffcc00", "size": 14, "covariance": true }
  },
  "tools": {
    "goal_topic": { "topic": "/goal_pose", "type": "geometry_msgs/msg/PoseStamped",
      "frame": "map" },
    "initial_pose": { "topic": "/initialpose",
      "type": "geometry_msgs/msg/PoseWithCovarianceStamped", "frame": "map" }
  },
  "transport": {
    "_comment": "边车是子进程；cmd 留空就找 exe 同目录和仓库的 build 目录",
    "cmd": "",
    "namespace": ""
  }
}
```

`transport.namespace` 非空时会作为 `NAV2_SHM_NAMESPACE` 传给子进程；留空就
继承当前 shell 的环境——**边车和 nav2 必须在同一个 namespace 里**，否则边车
看不到任何话题。

`qos` 现在只是被读进来、显示在面板里。**它真正起作用的位置是 Ubuntu 侧的
agent**：共享内存通道本身没有 QoS，可靠性/持久性只在 DDS 边界决定能不能匹配。
详见根 `.memories` 里「ARM64 / QoS」那一节。

保存时不是把结构体直接序列化：`_comment` 不是字段，直序列化会在第一次按
「保存」时把文件里所有注释删掉。实际做法是把新配置**深合并**进原文档，只覆盖
它提到的键，所以顶层和每个话题里的注释都会保留。

## 边车协议（`viz_feed`）

`viz.exe` 把 `viz_feed.exe` 作为**子进程**启动，走 stdin/stdout，不监听端口。
每一帧是：

```
uint32 小端长度 | uint8 类型（1=JSON，2=留给二进制）| 负载
```

Windows 上两端都把 stdio 切到二进制模式；否则 CRT 会把负载里的 `0x0A` 变成
`0x0D 0x0A`。stdout 只走帧，日志走 stderr。

命令（都带 `id`，回包带 `id`/`ok`/`result` 或 `error`）：

| 命令 | 作用 |
|---|---|
| `hello` | 协议版本、`NAV2_SHM_NAMESPACE`、当前 fixed frame |
| `topics` / `services` / `actions` / `nodes` | 读 shim graph（含 `remote:...` 源） |
| `frames` | TF 树里的坐标系名 |
| `subscribe` | `{topic, type?, transient_local?, max_hz?}`，之后推 `frame` 事件 |
| `unsubscribe` | 停止推送（保留通道，便于立刻恢复） |
| `set_fixed_frame` | 改固定系，**立刻重发**每个话题的最后一帧（latched 地图不用等新消息） |
| `publish_pose` | `{topic, type, frame, x, y, yaw, times?}`，只支持 `PoseStamped` / `PoseWithCovarianceStamped` |
| `shutdown` | 退出 |

边车认识的类型：`OccupancyGrid`、`LaserScan`、`Path`、`PoseStamped`、
`PoseWithCovarianceStamped`、`Odometry`、`Marker`、`MarkerArray`、
`nav2_msgs/Costmap`、`nav2_msgs/CostmapUpdate`、`map_msgs/OccupancyGridUpdate`、
`sensor_msgs/PointCloud2`。这些之外的
类型会得到 `no decoder`，不会被悄悄忽略。

`frame` 事件的 `kind` 与 `data`：

* `occupancy_grid`：`width/height/resolution/origin=[x,y,yaw]/data`（int8 的 base64）
* `laser_scan`：`points=[x0,y0,x1,y1,...]`（已变换到 fixed frame）
* `path`：`poses=[x0,y0,yaw0,...]`（已变换）
* `pose`：`x/y/yaw`（已变换）
* `pose_covariance`：`x/y/yaw/cov=[xx,xy,yy]/yaw_var`（协方差已旋转）
* `odometry`：`x/y/yaw/vx/wz/cov`，另有 `child_frame`
* `marker`：`ns/id/type/action/x/y/yaw/scale=[x,y,z]/color=[r,g,b,a]/points/text`；
  `points` 已经是固定系里的 `x,y` 对。`MarkerArray` 会拆成多条 `marker` 事件，
  同一 `ns/id` 的后一条覆盖前一条；`action=2` 删除，`action=3` 清空话题。
* `costmap`：`width/height/resolution/origin=[x,y,yaw]/layer/data`（代价值的数组）；
  `costmap_update`：`x/y/width/height/data`，合并进已有网格并让纹理失效
* `occupancy_grid_update`：同上，合并进已有的占据栅格
* `point_cloud2`：`points=[x0,y0,...]`（已变换到 fixed frame）、
  `heights=[...]`、`intensities=[...]`，以及包络 `min_x/min_y/max_x/max_y`

**TF 在边车里算**：边车是一个普通的 shim `rclcpp::Node`，带
`tf2_ros::Buffer` + `TransformListener`。每条消息用 `header.stamp` 查变换，
查不到再退回 latest；固定系改了就重发最后一帧。GUI 不重建 TF 树。

**单写者**：一个 shm 通道只能有一个写者。边车自己持有 `/goal_pose` 和
`/initialpose` 的写端（启动时就建好并登记 graph），所以这两个话题由 viewer
发；不要再对同一通道跑 `ros2 topic pub`，否则会各自截断对方的环形缓冲。

## 已验证 / 未验证

已验证（真窗口 + 真共享内存，端到端）：

* `viz.exe` 启动后自己拉起 `viz_feed.exe`；窗口关闭后子进程一起退出（实测
  `viz_feed` 残留 0 个）
* 发 `/tf_static`（`map -> laser`）、`/map`（20×20 占用栅格）、`/scan`
  （3 个回波）、`/plan`（3 个位姿）、`/amcl_pose`（带协方差）之后，窗口里：
  地图的占用边框被画出（32861 个 32,32,32 像素）、扫描点是 27 个
  0,255,136 像素、路径是 520 个 255,68,68 像素、机器人位姿是 6316 个
  255,204,0 像素

已验证（`go test`，无需显示器）：

* 面板收起后 `split.Offset >= 0.99`，展开回到**原来那个数**
* `_comment` 键被忽略、真实值覆盖默认值、缺文件用默认值
* 文件写了 `topics` 就替换默认表，没写才用默认；带 BOM 的文件能读
* 保存写的是当前窗口尺寸和分隔条位置；收起状态存的是展开宽度
* 保存保留顶层和话题内的 `_comment`（含带 BOM 的文件）；`Ctrl+O` 会应用窗口
  大小、分隔条和话题表
* 缩放时指针下的世界点不动
* 帧格式往返；超长帧被拒；真边车的 hello / topics（用独立 namespace）
* 地图纹理的调色板、协方差椭圆的半径、地图画在原点的位置、折线落在中线
* 代价地图的调色板（0 透明 / 高代价红 / 内切青 / 致命深色）、增量更新合并、
  点云的存储（点 / 高度 / 强度）与高度彩虹（低蓝高红）
* 工具：点击给位置、拖拽给朝向（向上拖 = +90°）、再点一次取消、无工具时
  拖拽仍然平移

已验证（真 ROS2 Jazzy 对端 `wang@192.168.0.153`）：

* `nav2_msgs/msg/Costmap`：`resolution` 0.5、3×2 网格、`origin` (1,2)、
  `layer` master、6 个代价值都对
* `sensor_msgs/msg/PointCloud2`：4 个点、z 0..1.5、包络 x 1..7 / y 2..8
* 点云的两种发布端都收到了：best_effort（相机）与 reliable（`ros2 topic pub`）
  —— agent 对点云订阅两次，各用一个回调，所以两边都收得到且不会重收

未验证 / 下一步：

* **鼠标点工具栏发点**（单元测试覆盖了坐标换算，边车的发布也单独验过，但
  「点工具栏图标 → 点视口 → 目标到达机器人」这条链还没在真窗口上跑通）
* 添加话题（从 graph 选）与显示属性面板
* 起完整 `nav2_stack.bat` 看真实 `/map` `/scan` `/plan`
* 点云的 per-point RGB / 法线；3D 与 URDF