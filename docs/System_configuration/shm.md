---
layout: default
title: 2 系统配置 · 共享内存几何配置
---

# 2.3 共享内存几何配置

<div class="overview">
<div class="label">本页总览</div>
<p>共享内存通道是 Windows 侧的本地总线。它的「几何」（槽数、槽大小）是
<strong>承诺不是上限</strong>，配大了会真的占掉那么多内存。本节说明怎么配、为什么这么配、
以及改完为什么必须一起重启。</p>
<ul>
  <li><a href="#what">通道与几何</a></li>
  <li><a href="#defaults">默认值与代价</a></li>
  <li><a href="#restart">为什么改完必须一起重启</a></li>
  <li><a href="#namespace">用命名空间隔离两套栈</a></li>
</ul>
</div>

## 2.3.1 通道与几何

| 项 | 值 |
| --- | --- |
| 通道名 | `ros2shim` + 话题名（可选前缀 `NAV2_SHM_NAMESPACE`） |
| 拓扑 | **单写者 + 多读者**，读者各自持有游标 |
| 落后太多 | 丢最旧的消息（`dropped` 计数） |
| 槽大小选择 | 名字含 `map`/`costmap`/`voxel` 的用大槽；含 `info` 的按小尺寸算 |

「单写者」是硬约定，不是性能取舍：守护进程镜像 `/scan` 时，本机就不能再有另一个进程发 `/scan`。

## 2.3.2 默认值与代价

<div class="callout warn">
<span class="label">重要</span>
<p>共享内存的尺寸是<strong>承诺</strong>不是上限：页文件支撑的 section 按整段提交，
所以给一条没人发消息的话题留的槽，是别的进程再也用不到的内存。</p>
</div>

曾经踩过的坑：按名字判断大小时，`map`/`costmap` 类话题被分配 **64 MB × 8 槽 = 512 MB**，
31 条话题让总占用到 **~2.4 GB**；`planner_server` 单独占 **6 GB**，
表现出来是 `bt_navigator` 里一个 `std::bad_alloc`。

现在 `bridge_env` 的取值：

| 变量 | 值 | 说明 |
| --- | --- | --- |
| `NAV2_SHM_SLOT_SIZE` | `524288`（512 KB） | 普通话题 |
| `NAV2_SHM_LARGE_SLOT_SIZE` | `2097152`（2 MB） | `map`/`costmap`/`voxel` |
| `NAV2_SHM_SLOT_COUNT` | `8` | 每条通道 |

实测最大消息是 `/global_costmap/costmap` 的 **528 KB**（不是 8 MB），所以 2 MB 大槽对它有 4 倍余量；
总占用降到 **~600 MB**，这台 7.6 GB 内存的机器不再 `CreateFileMapping failed (1455)`。

<div class="callout note">
<span class="label">注意</span>
<p><code>nav2/ros2_shim/include/rclcpp/shm_config.hpp</code> 里的<strong>默认值没动</strong>
（还是 2 MB / 8 MB），改的是 <code>bridge_env</code> 的覆盖值 —— 这是文档写明的覆盖机制，
也避免影响只跑 shim 的项目。</p>
</div>

### 节点自己发的两条

节点构造时会自己建两条 publisher，用户没有要求过：

| 话题 | 槽大小 | 为什么 |
| --- | --- | --- |
| `/parameter_events` | 固定 256 KB | 参数事件的长度不受控 |
| `/rosout` | 固定 256 KB | `Log` 的 `msg` 长度不受控 |

它们不受上面那条「按名字判断大小」的规则影响 —— 走默认小槽会把消息截断。

## 2.3.3 为什么改完必须一起重启

<div class="callout warn">
<span class="label">重要</span>
<p>槽几何一变，<strong>守护进程和所有 nav2 节点必须一起重启</strong>。
已经开着的通道保留旧布局，新老进程会按不同几何读写同一块内存。</p>
</div>

```powershell
# Windows：先停栈，再停守护进程，再重启
python .\bridge\scripts\run_nav2_stack.py stop
bridge\windows\ros2_bridge.bat stop
. .\start\加载模拟ROS环境变量.ps1
```

## 2.3.4 用命名空间隔离两套栈

要同时跑两套互不干扰的栈（例如一边跑 `ctest`、一边连着 Ubuntu 车），给其中一套加前缀：

```powershell
$env:NAV2_SHM_NAMESPACE = 'myspace'
```

通道名变成 `ros2shim<NAMESPACE><topic>`，两边各写各的。

<div class="callout note">
<span class="label">提示</span>
<p><code>shim</code> 树里的每条 ctest 都自动带一个
<code>NAV2_SHM_NAMESPACE=ctest_&lt;用例名&gt;</code>（<code>CMakeLists.txt</code> 末尾统一注入），
所以守护进程开着也不会和镜像进来的机器人抢同一条通道。</p>
</div>

## 2.3.5 排查：内存占用异常

```powershell
# 看当前进程占了多少
Get-Process planner_server, bt_navigator, controller_server -ErrorAction SilentlyContinue |
  Select-Object Name, @{n='WorkingSetMB';e={[math]::Round($_.WorkingSet64/1MB)}}
```

如果某条 `map`/`costmap` 通道占了几百 MB，先确认：

1. 守护进程和节点是不是都加载了同一份 `bridge_env`；
2. 有没有哪个进程退回了 2 MB / 8 MB 的默认值（用 `python tools/check_daemon_vs_relay.py`）。

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="network">2.2 网络与端口配置</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="../Basic_application/ros2_cli">3.1 ros2 命令行</a></div>
</div>