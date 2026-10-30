---
layout: default
title: 3 基础应用开发 · 地图存取
---

# 3.4 地图存取

<div class="overview">
<div class="label">本页总览</div>
<p>本节介绍地图的两种格式（PGM / 8 位 PNG）、怎么存、怎么读回来，以及
<code>map_server</code> 与 <code>map_saver</code> 的参数。</p>
<ul>
  <li><a href="#format">支持的图像格式</a></li>
  <li><a href="#save">存图</a></li>
  <li><a href="#load">读图</a></li>
  <li><a href="#server">map_server 参数</a></li>
</ul>
</div>

## 3.4.1 支持的图像格式

地图图像有**两个自己实现的 coder**（`include/Magick++.h` 替掉上游对 GraphicsMagick 的依赖）：

| 格式 | 读 | 写 |
| --- | --- | --- |
| **PGM** | P5（二进制）/ P2（ASCII） | P5 |
| **8 位 PNG** | 灰度 / RGB / RGBA / 灰度+alpha / 调色板五种颜色类型，五种行过滤器 | 灰度 或 灰度+alpha |

解压用工具链自带的 zlib（`-static` 让 `libz.a` 跟着进 exe，不新增 DLL）。

<div class="callout note">
<span class="label">注意</span>
<p><code>bmp</code> / <code>jpeg</code>、16 位或交错的 PNG <strong>仍然没有 coder</strong>，
会以一条点出格式名的错误失败。所以 <code>map_saver</code> / <code>map_saver_cli</code> 的
<code>--fmt</code> 实际可用的值是 <code>pgm</code> 和 <code>png</code>。</p>
</div>

## 3.4.2 存图

```bat
:: 存成 PGM + YAML
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt pgm

:: 存成 8 位 PNG + YAML（scale 模式要 PNG 才带透明通道）
nav2\ros2_shim\build\map_saver_cli.exe -t /map -f C:\maps\office --fmt png
```

产物是 `<名字>.pgm` / `<名字>.png` 加一个 `<名字>.yaml`：

```yaml
image: office.pgm
resolution: 0.05
origin: [0.0, 0.0, 0.0]
negate: 0
occupied_thresh: 0.65
free_thresh: 0.196
```

<div class="callout warn">
<span class="label">注意</span>
<p><code>slam_toolbox</code> 自带的 <code>map_saver.cpp</code> 走
<code>system("ros2 run nav2_map_server map_saver_cli ...")</code>，在 Windows 上<strong>必失败</strong>
—— 它要的是真的 <code>ros2</code> CLI 加上 ament 的包查找，而这里的 <code>ros2</code> 是替身。
要存图就直接用 <code>map_saver_cli.exe</code>。</p>
</div>

## 3.4.3 读图

```bat
:: 从文件把 /map 发出来
nav2\ros2_shim\build\map_server.exe --params-file nav2\ros2_shim\config\nav2_map_server.yaml ^
        -p yaml_filename:=C:\maps\office.yaml
```

或者在起 nav2 栈时直接指定（脚本会先起 `map_server` 再起 AMCL）：

```bat
python bridge\scripts\run_nav2_stack.py --map code\Desktop\racecar\map\ai_map001.yaml --goal 1.54,4.23 --timeout 90
```

<div class="callout note">
<span class="label">提示</span>
<p><code>--map FILE</code> 走的是 <code>map_server</code> 的 <code>yaml_filename</code>，
放在 <code>--params-file</code> <strong>之后</strong> —— 因为
<code>nav2_shim_overrides.yaml</code> 里的 <code>map_server.yaml_filename: ''</code>
先说话就会赢。</p>
</div>

## 3.4.4 map_server 参数

配置文件 `nav2/ros2_shim/config/nav2_map_server.yaml`：

| 参数 | 作用 |
| --- | --- |
| `yaml_filename` | 读哪个 YAML（命令行 `-p` 给） |
| `topic_name` | 发到哪个话题，默认 `/map` |
| `update_frequency` | 更新频率 |
| `subscribe_to_updates` | 是否订阅增量更新 |

<div class="callout note">
<span class="label">提示</span>
<p>存下来的图由 <code>map_server.exe</code> 读回来，与源文件<strong>逐字节相同</strong> ——
经 <code>map_server</code> 往返也是 0 处不同。</p>
</div>

## 3.4.5 vector_object_server

上游 <code>nav2_map_server</code> 的 vector object server 也移植了
（<code>apps/vector_object_server_node.cpp</code>）：被生命周期管理器推到 ACTIVE、
发出 <code>/vo_map</code>（<code>nav_msgs/msg/OccupancyGrid</code>）之后干净退出。

它要的 <code>uuid/uuid.h</code> 是本兼容层自己带的一份，不是系统库；
<code>ros2_shim_uuid</code> 这条 ctest 盯着它。

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="slam">3.3 SLAM 建图</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="../Architecture/overview">4.1 总体架构</a></div>
</div>
