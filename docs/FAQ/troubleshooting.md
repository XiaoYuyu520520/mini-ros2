---
layout: default
title: 6 常见问题 · 故障排查
---

# 6.2 故障排查

<div class="overview">
<div class="label">本页总览</div>
<p>本页按「症状 → 原因 → 怎么办」列出常见故障。</p>
<ul>
  <li><a href="#status">先看状态</a></li>
  <li><a href="#symptoms">按症状查</a></li>
  <li><a href="#scripts">验证脚本的写法</a></li>
</ul>
</div>

<a id="status"></a>

## 6.2.1 先看状态

```powershell
ros2 bridge status                     :: 只读图，不触发等待
bridge\windows\ros2_bridge.bat status  :: 守护进程状态 + 日志路径
```

<div class="callout warn">
<span class="label">重要</span>
<p>在只有一台机器、没连 Ubuntu 的时候，<code>ros2 topic list</code> 会<strong>自动拉起守护进程</strong>
并等它完成第一次同步，默认预算 30 s。本机没 Ubuntu 也没中继时，50060 端口没人监听 →
它会一直等到预算耗尽，工具调用就挂在那里。</p>
<p>本机验证时<strong>一律先关掉自动拉起</strong>：</p>
</div>

```powershell
$env:ROS2_BRIDGE_AUTOSTART = '0'    # 不要顺手拉守护进程
$env:ROS2_BRIDGE_WAIT_MS = '1500'   # 真要等就给个小的
```

<a id="symptoms"></a>

## 6.2.2 按症状查

### 「无法加载文件 …，因为在此系统上禁止运行脚本」

PowerShell 的默认执行策略是 `Restricted`，**任何 `.ps1` 都不让跑**。这不是脚本的问题，
是 Windows 的默认安全策略。解除方法：

```powershell
# 推荐：只对当前用户（不需要管理员，长期有效）
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned

# 或全局（需要管理员）
Set-ExecutionPolicy -Scope LocalMachine -ExecutionPolicy RemoteSigned

# 或只这一次（关窗口即失效）
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

如果 zip 是从浏览器下载的，脚本还带「来自 Internet」的标记，`RemoteSigned` 下仍可能被拦：

```powershell
Get-ChildItem -Path .\start, .\bridge -Recurse -Filter *.ps1 -ErrorAction SilentlyContinue |
  Unblock-File
```

详见 <a href="../Quick_start/windows">1.6.2 加载环境变量（PowerShell）</a>。

### 「ros2: 找不到接受自变量 'topic' 的位置参数」

这是旧版 `bridge/windows/ros2.ps1` 的一个 bug，**已修**：包装函数里那个接收剩余参数的
参数叫 `$Ros2Args`，而 PowerShell 允许用参数名的前缀来指定参数 —— `-r` 正好是
`Ros2Args` 的唯一前缀，于是 `ros2 topic pub ... -r 10` 里的 `-r 10` 被当成给这个参数赋值，
前面的 `topic pub ...` 反而没有位置可绑。

现在的 `ros2` 函数**不是** advanced function、也**没有** `param()` 块，
所有 token（包括 `-r`、`-t`、`-1`、`--once`）原样进 `$args` 转给 `ros2.exe`。

修好之后**当前窗口里的旧函数还在**，重新点源一次（或重开窗口）即可：

```powershell
. .\start\加载模拟ROS环境变量.ps1
ros2 topic pub /demo/chatter std_msgs/msg/String "{data: hello}" -r 1
```

### 「`ros2` 不是内部或外部命令」

**`.bat` 改不了 PowerShell 会话。** `.bat` 跑在 cmd.exe 子进程里，`set` 只影响那个子进程，
子进程一退变量跟着走。所以在 PowerShell 里执行 `.bat`，`echo %VAR%` 打得出来（那是 cmd 内部
展开的），但 `ros2` 仍然「不是内部或外部命令」—— PowerShell 的 PATH 从没被改过。

**正确做法：点源 `.ps1`**

```powershell
. E:\DesktopData\app\code\CPU\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

`.` 是必须的：**执行**不会把变量留在会话里，**点源**才会。不想点源就用
`bridge\windows\ros2_shell.bat`（开一个已经准备好的 PowerShell 窗口）。

<div class="callout warn">
<span class="label">关于 .ps1 的编码</span>
<p><strong><code>.ps1</code> 必须存成 UTF-8 BOM</strong>。Windows PowerShell 5.1 读没有 BOM 的
<code>.ps1</code> 时按 ANSI（cp936）解：中文变乱码，<strong>文件名里的中文变乱码 → 「找不到文件」</strong>，
更隐蔽的是<strong>逻辑也一起坏了</strong> —— 曾经 <code>$env:ROS2_BRIDGE_AGENT</code> 明明有
兜底赋值却始终为空，根因就是 5.1 把整个文件解码坏了。</p>
<p>用 <code>python tools/fix_ps1_encoding.py start</code> 统一改。</p>
</div>

### 「`topic list` 列不出远端话题」

按这个顺序查：

**① Ubuntu 侧起了吗？** 顺序反了 Windows 会扫不到。先
`bash bridge/scripts/ubuntu_sim_ctl.sh status` 或 `bash start/connector.sh status`。

**② 发现默认不看本机。** 守护进程和 agent 在同一台机器上时必须给地址：

```powershell
$env:ROS2_BRIDGE_AGENT = '127.0.0.1:50060'
```

**③ 等的是就绪文件，不是固定 sleep。** 冷启动实测 1.3–2.8 s；超过预算还没好，看守护进程日志：

```powershell
bridge\windows\ros2_bridge.bat log 40
```

**④ 冷启动首扫偶发扫不到对端**：有一次 `topic_list` 5.8 s 后报「远端话题全缺」，
而同一轮的其它检查全过，紧接着重跑就正常 —— 看着像首扫与对端 agent 的一次竞态，未定位。
**重跑一次**是最省事的对策。

### 「规划器对每个目标都报 `Start Coordinates ... was outside bounds`」

**起栈前忘了 `/sim/reset`。** AMCL 拿 `/odom` 当初始位姿，车停在别处就会这样，
**不重启栈不会自愈**。

```bat
ros2 service call /sim/reset std_srvs/srv/Trigger
```

### 「`remaining` 是 0.00 m、`/plan` x0、行为树直接报 SUCCEEDED」

车还停在上趟终点。**每趟 nav 之前先 `/sim/reset`** —— 那个 `travelled` 里的数只是上一趟的位移，
看着像过了，其实一步没走。

### 「`could not drive /map_server/change_state transition 3`」

上一轮留下的 `map_server.exe` 还在占着 `/map` 的写者。**跑 `--map` 之前先停掉上一轮的栈**：

```bat
python bridge\scripts\run_nav2_stack.py stop
```

另外 `/map` 只许一个写者：守护进程正在镜像 `/map` 时它会拒绝启动，并打印把 `/map` 摘出
镜像集的那两条命令。

### 「`bt_navigator` 激活不了」

行为树是**激活时**建的，叶子对应的 action server 不在，整个 `bt_navigator` 都激活不了 ——
所以 `lifecycle_manager` 的 `node_names` **必须列上 `behavior_server`**。

### 段错误，不是一条能读的报错

插件注册表的键**必须带命名空间**。`dwb_critics::PathAlignCritic` 与
`mppi::critics::PathAlignCritic` 是两个类；短名做键会互相覆盖，而覆盖掉的那个被查出来以后
是当**另一个基类**的指针用的（工厂函数里 `static_cast<Base *>` 的 `Base` 是注册时的基类），
虚调用直接跳进别的函数。

查找顺序是「带命名空间的拼写 → `normalize()` 之后」，见 `detail::lookup_keys()` /
`detail::find_factory()`，用例在 `tests/plugin_names.cpp`。

### 「行为树的目标被接受了，树却不动」

```powershell
$env:NAV2_SHIM_BT_TRACE = '1'      # 让 bt_navigator 定期打印每个节点的状态
$env:ROS2_SHIM_ACTION_TRACE = '1'  # 六条 action 通道上的每次收发
```

把每个节点的 `.log` 按时间排一遍，能直接看出「谁发了、谁没收到」。

`ROS2_SHIM_ACTION_TRACE=1` 每次发消息还会检查 `ShmChannel::publish()` 的返回值，超过槽大小的
载荷会打一行 `[act !! ] publish failed: N bytes > slot_size M`（这条**不受开关控制**，
丢消息永远是 bug）。

### 「action 的调用方一直等，不返回」

客户端自己**没有超时**（和上游一致），所以真丢了一条 ack/result，表现就是调用方一直等。
排查手段就是上面那条 `ROS2_SHIM_ACTION_TRACE`。

另外 **action 的环形缓冲只有 8 个槽**。`_feedback` 在跟随时是 ~40 Hz，也就是约 190 ms 的缓冲：
任何一次超过 190 ms 的调度延迟都会让读者跳过中间那些反馈。反馈丢几条无害，但这个
「慢读者静默丢消息」的性质是这套传输的固有性质。

### 「std::bad_alloc」

两个已知来源：

**① 共享内存几何。** `map`/`costmap` 类话题按 64 MB × 8 槽预留的话，一条 `map` 通道占 512 MB、
`planner_server` 单独占 6 GB，表现出来就是 `bt_navigator` 里一个 `std::bad_alloc`。
现在 `bridge_env` 的几何是 **512 KB / 2 MB / 64 KB**（总占用 ~600 MB）。
**改几何后守护进程和所有 nav2 节点必须一起重启**。

**② SMAC 的角度箱越界**（已修）。修前：对一个到不了的 goal 反复规划，第一次是正常的失败
（~770 ms 走到 `NoValidPath`），**第二次起每次 47 ms 抛 `std::bad_alloc`**。
修后同一个坏目标稳定报 `PlannerTimedOut: "exceeded maximum iterations"`。

### 「关停时 `std::terminate`」

`Costmap2DROS` 有一个 `std::unique_ptr<std::thread>` 的地图更新线程，只在 `on_deactivate()`
里 join。如果关停时节点还是 ACTIVE，派生类成员会先于基类析构销毁，线程在 joinable 状态下
析构会直接 `std::terminate`。

兼容层补的三条语义：**一个进程一个 Context**、**`remove_pre_shutdown_callback()` 真的摘除**、
**`rclcpp::shutdown()` 先跑回调再清 `ok` 标志**。

### 「`now()` 和消息戳差了 56 年」

**没打开 `use_sim_time` 的节点**，`now()` 是系统墙钟（1.79e9 s），而 Ubuntu 仿真把消息戳写成
**仿真时间**（从 0 开始）。这类节点里凡是把 `now()` 和消息戳配在一起的代码都在向未来外推。

栈起来的那几个节点（`amcl` / `planner_server` / `controller_server` / `bt_navigator` /
`behavior_server` / `smoother_server` / `waypoint_follower`）都在 `use_sim_time` 下跑。

### 「`ros2 topic pub` 的频率只有 64 Hz」

Windows 默认定时器粒度 15.6 ms，所以按 `sleep_for(1000/rate)` **毫秒**睡会退化成 ~64 Hz。
现在改成按 deadline 推进（`rclcpp/precise_wait.hpp` 的 `wait_until()`），实测 1000 Hz → 1002.7 Hz。

<div class="callout note">
<span class="label">但</span>
<p><code>rclcpp</code> 的 executor 轮询循环里还有 <code>sleep_for(1ms)</code>，所以
<strong>节点自己的定时器</strong>最高仍约 64 Hz。nav2 的 20–50 Hz 不受影响，
但 kHz 级节点定时器要另改。</p>
</div>

### 「`ros2 topic echo` 说没有发布者就退出了」

标准 ROS2 是**创建订阅然后等**。现在改成用 `create_channel()`（shim 自己的规则：两个进程谁先碰
都行），并且在等待期间**惰性解析类型** —— 发布者出现后 `refreshSubscriberType()` 把订阅记录的
类型补上。

### Linux 上的 `bash` 不可用

这台机器上的 `bash` 是 WSL 的 `bash.EXE`，但**没有装任何 WSL 发行版**，所以 `bash -n xxx.sh`
只会打印「适用于 Linux 的 Windows 子系统没有已安装的分发版」。`.sh` 的语法检查改用 Python 做
关键字配平（if/fi、for/done、case/esac，注意跳过 heredoc 体）。

### 上游源码不能改

改 `nav2/nav2_extracted/` 里的东西会被 `nav2/patches/verify_patch.py` 拦下来。规矩是：
**能不改就不改**（加在 `prelude.hpp` / `include\ros2_shim\`）；非改不可的，写成
`nav2/patches/` 里的 patch 让 `verify_patch.py` 能验。

### 还没定位 / 没有证据的

| 现象 | 状态 |
| --- | --- |
| 冷启动首扫偶发扫不到对端 | 看着像竞态，未定位 —— **重跑一次** |
| 守护进程断线重连 | **没有测试碰过**；长时间跑会不会漏内存也没数据 |
| `slam_toolbox` 的 `map_saver.cpp` | 走 `system("ros2 run ...")`，Windows 上必失败 —— 用 `map_saver_cli.exe` |
| 窄道 / 回环 / 长时间漂移 | 没测 |

<a id="scripts"></a>

## 6.2.3 验证脚本的写法

<div class="callout warn">
<span class="label">不要用 cmd /c 验证环境变量</span>
<p><code>cmd /c '... &amp;&amp; echo %VAR%'</code> —— <code>%VAR%</code> 在<strong>整条命令行解析时</strong>
就展开了，比 <code>call</code> 早，看起来像「没生效」，其实是测试写法错。
用延迟展开 <code>cmd /v:on /c '... &amp; echo !VAR!'</code>，
或者更直接：在 PowerShell 里点源 <code>.ps1</code> 后读 <code>$env:VAR</code>。</p>
</div>

* **测试脚本要写成文件再 `-File` 执行**，不要把中文路径塞进 `powershell -Command` ——
  bash → powershell 的命令行会把它打散。写成文件后用 `Get-ChildItem -Filter '*.ps1'` +
  ASCII 通配定位目标，中文路径就不经过命令行了。
* 验证脚本要**加载 bridge_env**：用 `bridge/scripts/_bridge_env.py` 读 `bridge_env.bat` 的变量。
  **不能**用 `cmd /c "call x.bat &amp;&amp; set"`（cmd 会给含引号的命令行重新加引号，
  `call "path"` 被转义坏掉）—— 改用**临时 .bat 包装**（`call ...` + `set`）绕开引号。
  否则守护进程拿不到槽几何，退回 2 MB / 8 MB 的默认值。

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="limitations">6.1 功能限制与缺口</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="../Advanced/development">7.1 开发指南</a></div>
</div>
