# viz — Windows 上的共享内存 ROS2 可视化（空壳）

这是 **外壳**：一个能拖动、能缩放、能收起面板的 2D 视口。真正喂数据给它的
C++ 边车（`viz_feed.exe`）还没写，所以现在它只画网格。

```
go run .                       # 读可执行文件旁边或当前目录的 config.json
go run . -config other.json
```

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

**DPI**：Fyne 自带 per-monitor DPI 感知（实测进程 `dpiAware=2`）。视口的位图
按「逻辑尺寸 × 画布缩放」生成再铺满控件，所以高 DPI 下网格是清晰的，不是被
主题放大糊掉。`FYNE_SCALE=2` 是手动覆盖，用来验证缩放路径。

## 配置

`config.json` 里所有键都是可选的，缺的用默认值补。**JSON 没有注释语法**，
所以注释写成以 `_comment` 开头的键——`encoding/json` 会忽略不认识的键。

```json
{
  "_comment": "这一行是注释",
  "window": { "width": 1440, "height": 900 },
  "layout": { "panel_offset": 0.74 },
  "views": { "view": { "grid_cell_m": 1.0, "zoom": { "min": 0.02, "max": 40 } } },
  "topics": {
    "/scan": {
      "type": "sensor_msgs/msg/LaserScan",
      "qos": { "reliability": "best_effort", "depth": 5 },
      "color": "#00ff88"
    }
  }
}
```

`qos` 现在只是被读进来、显示在面板里。**它真正起作用的位置是 Ubuntu 侧的
agent**：共享内存通道本身没有 QoS，可靠性/持久性只在 DDS 边界决定能不能匹配。
详见根 `.memories` 里「ARM64 / QoS」那一节。

## 已验证 / 未验证

已验证（`go test`，无需显示器）：

* 面板收起后 `split.Offset >= 0.99`，展开回到**原来那个数**
* `_comment` 键被忽略、真实值覆盖默认值、缺文件用默认值
* 缩放时指针下的世界点不动

已验证（真窗口，脚本采样）：

* 进程 DPI 感知 = 2（per-monitor）
* 窗口从 1456×939 拖到 1000×700 后仍正常
* `FYNE_SCALE=1` 网格间距 40 px，`=2` 时 80 px（缩放路径正确）

未验证 / 下一步：

* 边车（`viz_feed.exe`）：话题列表、订阅、解码、发 goal / initial pose
* 地图 / 扫描 / 路径的实际绘制
* 真 ROS2 上 best_effort / transient_local 的 QoS 匹配