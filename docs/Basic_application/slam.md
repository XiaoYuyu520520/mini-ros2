---
layout: default
title: 3 基础应用开发 · SLAM 建图
---

# 3.3 SLAM 建图

<div class="overview">
<div class="label">本页总览</div>
<p>本节介绍用 <code>slam_toolbox</code> 在 Windows 上建图、怎么存图、怎么把存下来的图喂回去定位。</p>
<ul>
  <li><a href="#start">启动建图</a></li>
  <li><a href="#drive">怎么走出能建出图的轨迹</a></li>
  <li><a href="#params">参数</a></li>
  <li><a href="#archive">位姿图存档</a></li>
</ul>
</div>

<a id="start"></a>

## 3.3.1 启动建图

### [Windows]

```bat
python bridge\scripts\run_slam_demo.py --duration 60
:: 等价的 cmd 外壳
bridge\windows\slam_demo.bat --duration 60
```

### [Linux]

```bash
python3 bridge/scripts/run_slam_demo.py --duration 60
```

它前台跑，<code>--duration</code> 到点自己退。

<div class="callout warn">
<span class="label">重要</span>
<p><strong><code>/map</code> 只许一个写者。</strong>守护进程正在镜像 <code>/map</code> 时它会
<strong>拒绝启动</strong>，并打印把 <code>/map</code> 摘出镜像集的那两条命令 ——
照着执行，再重跑建图。</p>
</div>

### 存好图再导航

```bat
:: 存图（PGM 或 8 位 PNG）
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt pgm
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt png

:: 把存下来的图发回 /map
nav2\ros2_shim\build\map_server.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml ^
        -p yaml_filename:=C:\maps\office.yaml
```

或者用 racecar 的封装（Windows 与 Ubuntu 同一份）：

```bat
python code\Desktop\racecar\tools\racecar_ctl.py save-map -f ai_map001
python code\Desktop\racecar\tools\racecar_ctl.py nav --map ai_map001 --goal auto
```

<div class="callout note">
<span class="label">提示</span>
<p><code>mode: scale</code> 要 PNG 才存得下透明通道 —— 上游 <code>checkSaveParameters()</code>
在那个模式下默认就是 <code>png</code>，<code>slam_toolbox</code> 自带的配置里
<code>image_format</code> 也是 <code>png</code>。</p>
</div>

<a id="drive"></a>

## 3.3.2 怎么走出能建出图的轨迹

<div class="callout warn">
<span class="label">重要</span>
<p><code>slam_toolbox</code> 有三个<strong>运动门槛</strong>，出厂值是
<code>0.5 / 0.5 / 0.5</code>（真车值）。车停着不动，整个跑车过程只提交 <strong>1</strong> 张扫描；
一趟 4.5 m 只提交 <strong>9</strong> 张。而 karto 的 <code>OccupancyGrid::UpdateCell()</code> 要
<code>cellPassCnt &gt; min_pass_through</code>(2) 才写格子 —— 于是只有车周围 ~1.7 m 的一圈格子进图，
<strong>占据格子恒为 0</strong>（每面墙的每格只被一张扫描打到，<code>hitCnt = 1</code>）。</p>
</div>

所以这三个值要降到 <code>0.1</code>：

```yaml
# code/Desktop/racecar/src/racecar/config/slam_toolbox.yaml
minimum_travel_distance: 0.1
minimum_travel_heading: 0.1
minimum_time_interval: 0.1
```

改完的效果（走一条 4.5 m 见方的环线）：整间 12 m 房建出来 ——
<code>233x233</code>、free **82.8%** / unknown 14.9% / occupied **2.4%**（1286 格）。

<div class="callout note">
<span class="label">提示</span>
<p>另外 <code>run_sim</code> 的 <code>--world-size</code> 现在默认 <code>12.0</code>
（与 <code>--range</code> 相等，墙在半量程处）。旧默认 <code>24.0</code> 让四面墙全落在
12 m 量程之外。</p>
</div>

<a id="params"></a>

## 3.3.3 参数

```bat
:: slam_toolbox 默认不读 yaml，只给 -p
bridge\windows\slam_demo.bat -p base_frame:=base_link

:: 要文件就自己传（传了就清掉默认 -p）
bridge\windows\slam_demo.bat --params-file my_slam.yaml
```

跑完一圈之后，三种拿法像素一致：

| 方式 | 产物 |
| --- | --- |
| `map_dump.exe` | PGM |
| `map_saver_cli.exe --fmt pgm` | PGM + YAML |
| `map_saver_cli.exe --fmt png` | 8 位 PNG + YAML |

经 `map_server` 往返与源文件 **0 处不同**。

<a id="archive"></a>

## 3.3.4 位姿图存档

slam_toolbox 的 **localization / lifelong 模式**需要把位姿图存下来：

```bat
:: 存
ros2 service call slam_toolbox/serialize_map slam_toolbox/srv/SerializePoseGraph filename=C:/maps/office

:: 读（两个入口都验过）
bridge\windows\slam_demo.bat -p map_file_name:=C:/maps/office -p map_start_at_dock:=true
ros2 service call slam_toolbox/deserialize_map slam_toolbox/srv/DeserializePoseGraph filename=C:/maps/office
```

实测：位姿图 `7 318 952` 字节 <code>.posegraph</code> + `385 332` 字节 <code>.data</code>，
解档后逐像素比对与建图那趟的 `322 x 473` 只差 0.30% / 0.083%（差值来自解档之后继续进来的新扫描）。

<div class="callout note">
<span class="label">提示</span>
<p>不给初始位姿的话，节点照跑、<code>/map</code> 照发，但<strong>图是空的</strong>
（<code>occupied=0</code>）。用 <code>map_start_at_dock:=true</code> 或
<code>map_start_pose:=[0.0,0.0,0.0]</code>。</p>
</div>

## 3.3.5 定位（AMCL）

用 AMCL 在存好的图上定位：

```bat
python bridge\scripts\run_amcl_demo.py --initial-pose 1.5,-2,0
:: 等价的 cmd 外壳
bridge\windows\amcl_demo.bat --initial-pose 1.5,-2,0
```

栈里已经含 AMCL，一般不用单独起。

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="nav2_stack">3.2 运行 nav2 导航栈</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="map">3.4 地图存取</a></div>
</div>
