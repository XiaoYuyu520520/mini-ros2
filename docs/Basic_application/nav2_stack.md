---
layout: default
title: 3 基础应用开发 · 运行 nav2 导航栈
---

# 3.2 运行 nav2 导航栈

<div class="overview">
<div class="label">本页总览</div>
<p>本节介绍怎么把上游 nav2 那一整套服务器（AMCL + 规划 + 控制 + 行为树 + 平滑 + 航点 + 生命周期）
拉起来、指挥小车开到目标点，以及怎么把安全链接进驱动链。</p>
<ul>
  <li><a href="#prereq">前置条件</a></li>
  <li><a href="#start">启动</a></li>
  <li><a href="#params">参数从哪来</a></li>
  <li><a href="#safety">把安全链接进链</a></li>
  <li><a href="#switch">运行期换控制器</a></li>
  <li><a href="#stop">停止</a></li>
</ul>
</div>

## 3.2.1 前置条件

<div class="callout warn">
<span class="label">重要</span>
<p>起栈前<strong>先把小车放回原点</strong>：</p>
</div>

```bat
bridge\windows\ros2.bat service call /sim/reset std_srvs/srv/Trigger
```

AMCL 拿 `/odom` 当初始位姿。车停在别处时，每一次规划都会报
`Start Coordinates ... was outside bounds`，**不重启栈不会自愈**。

## 3.2.2 启动

### [Windows]

```bat
:: 首选：Python 入口（Windows 与 Ubuntu 同一份）
python bridge\scripts\run_nav2_stack.py --goal auto --report goal.json

:: 等价的 cmd 外壳
bridge\windows\nav2_stack.bat --goal auto --report goal.json
```

### [Linux]

```bash
python3 bridge/scripts/run_nav2_stack.py --goal auto --report /tmp/goal.json
```

### 常用开关

| 开关 | 作用 |
| --- | --- |
| `--goal auto` | 自己挑一个可达目标开过去 |
| `--goal X,Y` | 指定目标 |
| `--map FILE` | 拿存好的图（PGM + YAML）当 `/map` |
| `--safety` | 把速度平滑 + 碰撞监控接进驱动链 |
| `--safety-params FILE` | 再叠一份参数（改限速和停止多边形） |
| `--duration N` | 跑 N 秒后自动退 |
| `--params FILE` | 换掉上游那份 `nav2_params.yaml` |
| `--no-overrides` | 不叠兼容层的默认覆写 |
| `stop` | 停掉这个栈起的七个进程 |

<div class="callout note">
<span class="label">提示</span>
<p>日志一节点一个文件（Windows <code>%TEMP%\nav2_stack\</code>、Ubuntu <code>/tmp/nav2_stack/</code>）。
「栈活了」看 <code>lifecycle_manager.log</code> 里的 <code>Managed nodes are active</code>。</p>
</div>

### 栈起来的是什么

| 节点 | 作用 |
| --- | --- |
| `amcl_bridge_node` | 粒子滤波定位，发 `map -> odom` |
| `planner_server` | 全局规划（NavFn / Smac Hybrid-A*） |
| `controller_server` | 局部控制（RPP / DWB / MPPI / Graceful） |
| `bt_navigator` | 行为树导航（`navigate_to_pose` / `navigate_through_poses`） |
| `behavior_server` | 自救行为（`Spin` / `BackUp` / `Wait` / …） |
| `smoother_server` | 路径平滑 |
| `waypoint_follower` | 航点巡航 |
| `lifecycle_manager` | 把上面这些推到 ACTIVE |

<div class="callout note">
<span class="label">重要</span>
<p><code>lifecycle_manager</code> 的 <code>node_names</code> <strong>必须列上
<code>behavior_server</code></strong> —— 行为树是<strong>激活时</strong>建的，
叶子对应的 action server 不在，整个 <code>bt_navigator</code> 都激活不了。</p>
</div>

### 为什么顺序是硬约束

三种角色的起点不同，所以顺序在这里是硬约束：服务器起来是 unconfigured 的，靠管理器推到 active；
而管理器推得动的前提是两个 costmap 能在 `on_activate()` 里解析出 `map -> base_link`，
那条链的第一段（`map -> odom`）<strong>只有 AMCL 能给</strong>。

栈里没有 AMCL 时，两个 costmap 会在 `on_activate()` 里等 `base_link -> map` 等到超时。

## 3.2.3 参数从哪来

| 谁 | 加载什么 | 文件 |
| --- | --- | --- |
| nav2 栈（7 个服务器） | ① 上游主参数 ② 叠加层（**最后加载、覆盖上游**）③ `lifecycle_manager` 只读叠加层 | ① `nav2/nav2_extracted/navigation2-1.5.1/nav2_bringup/params/nav2_params.yaml` ② ③ `nav2/ros2_shim/config/nav2_shim_overrides.yaml` |
| 行为树 | `$(env NAV2_SHIM_BT_DIR)` 指向的 XML | `nav2/ros2_shim/config/bt/navigate_to_pose.xml`、`navigate_through_poses.xml` |

<div class="callout warn">
<span class="label">重要</span>
<p>换参数优先改 <code>nav2/ros2_shim/config/nav2_shim_overrides.yaml</code>（它本来就是干这个的）。
<strong>不要去改上游的 <code>nav2_params.yaml</code></strong> —— 那是原样保留的副本，
<code>nav2/patches/verify_patch.py</code> 和文档都以「上游只有一处例外」为前提。</p>
</div>

## 3.2.4 把安全链接进链

`nav2_velocity_smoother` + `nav2_collision_monitor` 有两种接法：

| 接法 | 命令 | 效果 |
| --- | --- | --- |
| **旁路** | `python bridge\scripts\run_nav2_safety.py` | `/cmd_vel` → `/cmd_vel_smoothed` → `/cmd_vel_monitored`，两条输出都没有第二个写者、**不动车**。用来量测与观察 |
| **进链** | `python bridge\scripts\run_nav2_stack.py --safety --goal auto` | `controller_server` / `behavior_server` 被 remap 到 `/cmd_vel_nav`，smoother 跟过去，`collision_monitor` 的 `cmd_vel_out_topic` 指回 `/cmd_vel`，成为它的**唯一写者** |

<div class="callout note">
<span class="label">实测</span>
<p>进链之后，把停止多边形放大到 12 m 时，同一个目标从「走 2.39 m」变成「<strong>走 0.00 m</strong>」，
而控制器一直在发 0.45 m/s —— 这一层<strong>就是车的命运</strong>。</p>
</div>

`/collision_monitor_state`（`nav2_msgs/msg/CollisionMonitorState`）已经进了编解码表：

```bat
ros2 topic echo /collision_monitor_state
::   action_type: 2 (slowdown)
::   polygon_name: "StopPolygon"
```

## 3.2.5 运行期换控制器

四个控制器在同一个栈里并存：

```yaml
controller_plugins: ["FollowPath", "DWBFollowPath", "MPPIFollowPath", "GracefulFollowPath"]
```

运行期切换（栈不必重启）：

```bat
ros2 topic pub /controller_selector std_msgs/msg/String data=MPPIFollowPath --once
```

| id | 内容 |
| --- | --- |
| `FollowPath` | `nav2_regulated_pure_pursuit_controller`（RPP，默认） |
| `DWBFollowPath` | `nav2_dwb_controller` |
| `MPPIFollowPath` | `nav2_mppi_controller`（8 个 critic） |
| `GracefulFollowPath` | `nav2_graceful_controller` |

<div class="callout note">
<span class="label">提示</span>
<p>还有 <code>nav2_rotation_shim_controller</code>（先原地转向再交给内部的 RPP）也编好了，
但它占的就是 <code>FollowPath</code> 这个 id，要用自己那份参数文件起栈（<code>--no-overrides</code>）。</p>
</div>

## 3.2.6 停止

```bat
python bridge\scripts\run_nav2_stack.py stop
python bridge\scripts\run_nav2_safety.py stop
bridge\windows\ros2_bridge.bat stop
taskkill /F /IM ros2.exe        :: 残留的发布者：ros2 topic pub 不带 --once 会一直发
```

Linux 侧：

```bash
python3 bridge/scripts/run_nav2_stack.py stop
bash bridge/linux/ros2_bridge.sh stop
```

<div class="callout warn">
<span class="label">注意</span>
<p><strong>跑 <code>--map</code> 之前先停掉上一轮的栈。</strong>上一轮留下的
<code>map_server.exe</code> 会继续占着 <code>/map</code> 的写者，新一轮起栈就卡在
<code>[nav2_stack] could not drive /map_server/change_state transition 3</code>。</p>
</div>

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="ros2_cli">3.1 ros2 命令行</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="slam">3.3 SLAM 建图</a></div>
</div>
