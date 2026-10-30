---
layout: default
title: 排错 Troubleshooting
---

# 排错 Troubleshooting

> **[中文](#中文) · [English](#english)**

---

# 中文

## 先看状态

```powershell
ros2 bridge status                     :: 只读图，不触发等待
bridge\windows\ros2_bridge.bat status  :: 守护进程状态 + 日志路径
```

> **在只有一台机器、没连 Ubuntu 的时候**，`ros2 topic list` 会**自动拉起守护进程**并等它
> 完成第一次同步，默认预算 30 s。本机没 Ubuntu 也没中继时，50060 端口没人监听 →
> 它会一直等到预算耗尽，工具调用就挂在那里。
> 本机验证时**一律先关掉自动拉起**：

```powershell
$env:ROS2_BRIDGE_AUTOSTART = '0'    # 不要顺手拉守护进程
$env:ROS2_BRIDGE_WAIT_MS = '1500'   # 真要等就给个小的
```

## Windows 上「`ros2` 不是内部或外部命令」

**`.bat` 改不了 PowerShell 会话。** `.bat` 跑在 cmd.exe 子进程里，`set` 只影响那个子进程，
子进程一退变量跟着走。所以在 PowerShell 里执行 `.bat`，`echo %VAR%` 打得出来（那是 cmd 内部
展开的），但 `ros2` 仍然「不是内部或外部命令」—— PowerShell 的 PATH 从没被改过。

**正确做法：点源 `.ps1`**

```powershell
. E:\DesktopData\app\code\CPU\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

`.` 是必须的：**执行**不会把变量留在会话里，**点源**才会。不想点源就用
`bridge\windows\ros2_shell.bat`（开一个已经准备好的 PowerShell 窗口），或者一次一条命令用
`bridge\windows\ros2.bat topic list -t`。

> **`.ps1` 必须存成 UTF-8 BOM**。Windows PowerShell 5.1 读没有 BOM 的 `.ps1` 时按 ANSI
> （cp936）解，中文变乱码，**文件名里的中文变乱码 → 「找不到文件」**，更隐蔽的是
> **逻辑也一起坏了** —— 曾经 `$env:ROS2_BRIDGE_AGENT` 明明有兜底赋值却始终为空，
> 根因就是 5.1 把整个文件解码坏了。用 `python tools/fix_ps1_encoding.py start` 统一改。

## 「`topic list` 列不出远端话题」

按这个顺序查：

1. **Ubuntu 侧起了吗？** 顺序反了 Windows 会扫不到。先 `bash bridge/scripts/ubuntu_sim_ctl.sh status`
   或 `bash start/connector.sh status`。
2. **发现默认不看本机**。守护进程和 agent 在同一台机器上时必须给地址：
   `ROS2_BRIDGE_AGENT=127.0.0.1:50060`（或 `:50051`）。
3. **等的是就绪文件，不是固定 sleep**。冷启动实测 1.3–2.8 s；超过预算还没好，看守护进程日志。
4. **冷启动首扫偶发扫不到对端**：有一次 `topic_list` 5.8 s 后报「远端话题全缺」，
   而同一轮的其它检查全过，紧接着重跑就正常 —— 看着像首扫与对端 agent 的一次竞态，未定位。
   **重跑一次**是最省事的对策。

## 「规划器对每个目标都报 `Start Coordinates ... was outside bounds`」

**起栈前忘了 `/sim/reset`**。AMCL 拿 `/odom` 当初始位姿，车停在别处就会这样，
**不重启栈不会自愈**。

```bat
ros2 service call /sim/reset std_srvs/srv/Trigger
```

## 「`remaining` 是 0.00 m、`/plan` x0、行为树直接报 SUCCEEDED」

车还停在上趟终点。**每趟 nav 之前先 `/sim/reset`** —— 那个 `travelled` 里的数只是上一趟的位移，
看着像过了，其实一步没走。

## 「`could not drive /map_server/change_state transition 3`」

上一轮留下的 `map_server.exe` 还在占着 `/map` 的写者。**跑 `--map` 之前先停掉上一轮的栈**：

```bat
python bridge\scripts\run_nav2_stack.py stop
```

另外 `/map` 只许一个写者：守护进程正在镜像 `/map` 时它会拒绝启动，并打印把 `/map` 摘出
镜像集的那两条命令。

## 「`bt_navigator` 激活不了」

行为树是**激活时**建的，叶子对应的 action server 不在，整个 `bt_navigator` 都激活不了 ——
所以 `lifecycle_manager` 的 `node_names` **必须列上 `behavior_server`**。

## 「段错误，不是一条能读的报错」

插件注册表的键**必须带命名空间**。`dwb_critics::PathAlignCritic` 与
`mppi::critics::PathAlignCritic` 是两个类；短名做键会互相覆盖，而覆盖掉的那个被查出来以后
是当**另一个基类**的指针用的（工厂函数里 `static_cast<Base *>` 的 `Base` 是注册时的基类），
虚调用直接跳进别的函数。

查找顺序是「带命名空间的拼写 → `normalize()` 之后」，见 `detail::lookup_keys()` /
`detail::find_factory()`，用例在 `tests/plugin_names.cpp`。

## 「行为树的目标被接受了，树却不动」

```powershell
$env:NAV2_SHIM_BT_TRACE = '1'      # 让 bt_navigator 定期打印每个节点的状态
$env:ROS2_SHIM_ACTION_TRACE = '1'  # 六条 action 通道上的每次收发
```

把每个节点的 `.log` 按时间排一遍，能直接看出「谁发了、谁没收到」。

`ROS2_SHIM_ACTION_TRACE=1` 每次发消息还会检查 `ShmChannel::publish()` 的返回值，超过槽大小的
载荷会打一行 `[act !! ] publish failed: N bytes > slot_size M`（这条**不受开关控制**，丢消息
永远是 bug）。

## 「action 的调用方一直等，不返回」

客户端自己**没有超时**（和上游一致），所以真丢了一条 ack/result，表现就是调用方一直等。
排查手段就是上面那条 `ROS2_SHIM_ACTION_TRACE`。

另外 **action 的环形缓冲只有 8 个槽**。`_feedback` 在跟随时是 ~40 Hz，也就是约 190 ms 的缓冲：
任何一次超过 190 ms 的调度延迟都会让读者跳过中间那些反馈。反馈丢几条无害，但这个
「慢读者静默丢消息」的性质是这套传输的固有性质。

## 「std::bad_alloc」

两个已知来源：

* **共享内存几何**：`map`/`costmap` 类话题按 64 MB × 8 槽预留的话，一条 `map` 通道占 512 MB、
  `planner_server` 单独占 6 GB，表现出来就是 `bt_navigator` 里一个 `std::bad_alloc`。
  现在 `bridge_env` 把几何改成 **512 KB / 2 MB / 64 KB**（总占用 ~600 MB）。
  **改几何后守护进程和所有 nav2 节点必须一起重启**（已开着的通道保留旧布局）。
* **SMAC 的角度箱越界**（已修）。修前：对一个到不了的 goal 反复规划，第一次是正常的失败
  （~770 ms 走到 `NoValidPath`），**第二次起每次 47 ms 抛 `std::bad_alloc`**。
  用 gdb 的 `__cxa_throw` catchpoint 钉住分配点，四处越界全在 `nav2_smac_planner/` 里。
  修后同一个坏目标稳定报 `PlannerTimedOut: "exceeded maximum iterations"`。

## 「关停时 `std::terminate`」

`Costmap2DROS` 有一个 `std::unique_ptr<std::thread>` 的地图更新线程，只在 `on_deactivate()`
里 join。如果关停时节点还是 ACTIVE，派生类成员会先于基类析构销毁，线程在 joinable 状态下
析构会直接 `std::terminate`。

兼容层补的三条语义：**一个进程一个 Context**、**`remove_pre_shutdown_callback()` 真的摘除**、
**`rclcpp::shutdown()` 先跑回调再清 `ok` 标志**。

## 「`now()` 和消息戳差了 56 年」

**没打开 `use_sim_time` 的节点**，`now()` 是系统墙钟（1.79e9 s），而 Ubuntu 仿真把消息戳写成
**仿真时间**（从 0 开始）。这类节点里凡是把 `now()` 和消息戳配在一起的代码都在向未来外推。

栈起来的那几个节点（`amcl` / `planner_server` / `controller_server` / `bt_navigator` /
`behavior_server` / `smoother_server` / `waypoint_follower`）都在 `use_sim_time` 下跑。

## 「`ros2 topic pub` 的频率只有 64 Hz」

Windows 默认定时器粒度 15.6 ms，所以按 `sleep_for(1000/rate)` **毫秒**睡会退化成 ~64 Hz。
现在改成按 deadline 推进（`rclcpp/precise_wait.hpp` 的 `wait_until()`），实测 1000 Hz → 1002.7 Hz。

> **但** `rclcpp` 的 executor 轮询循环里还有 `sleep_for(1ms)`，所以**节点自己的定时器**最高仍
> 约 64 Hz。nav2 的 20–50 Hz 不受影响，但 kHz 级节点定时器要另改。

## 「`ros2 topic echo` 说没有发布者就退出了」

标准 ROS2 是**创建订阅然后等**。现在改成用 `create_channel()`（shim 自己的规则：两个进程谁先碰
都行），并且在等待期间**惰性解析类型** —— 发布者出现后 `refreshSubscriberType()` 把订阅记录的
类型补上。

## Linux 上的 `bash` 不可用

这台机器上的 `bash` 是 WSL 的 `bash.EXE`，但**没有装任何 WSL 发行版**，所以 `bash -n xxx.sh`
只会打印「适用于 Linux 的 Windows 子系统没有已安装的分发版」。`.sh` 的语法检查改用 Python 做
关键字配平（if/fi、for/done、case/esac，注意跳过 heredoc 体）。

## 验证脚本的写法

* **不要**用 `cmd /c '... && echo %VAR%'` —— `%VAR%` 在**整条命令行解析时**就展开了，比 `call`
  早，看起来像「没生效」，其实是测试写法错。用延迟展开 `cmd /v:on /c '... & echo !VAR!'`，
  或者更直接：在 PowerShell 里点源 `.ps1` 后读 `$env:VAR`。
* **测试脚本要写成文件再 `-File` 执行**，不要把中文路径塞进 `powershell -Command` ——
  bash → powershell 的命令行会把它打散。写成文件后用 `Get-ChildItem -Filter '*.ps1'` +
  ASCII 通配定位目标，中文路径就不经过命令行了。
* 验证脚本要**加载 bridge_env**：用 `bridge/scripts/_bridge_env.py` 读 `bridge_env.bat` 的变量。
  **不能**用 `cmd /c "call x.bat && set"`（cmd 会给含引号的命令行重新加引号，`call "path"` 被
  转义坏掉）—— 改用**临时 .bat 包装**（`call ...` + `set`）绕开引号。否则守护进程拿不到槽
  几何，退回 2 MB / 8 MB 的默认值。

## 上游源码不能改

改 `nav2/nav2_extracted/` 里的东西会被 `nav2/patches/verify_patch.py` 拦下来。规矩是：
**能不改就不改**（加在 `prelude.hpp` / `include\ros2_shim\`）；非改不可的，写成
`nav2/patches/` 里的 patch 让 `verify_patch.py` 能验。

## 还没定位 / 没有证据的

有些现象**知道存在但没查清**，遇到时先绕开：

| 现象 | 状态 |
| --- | --- |
| 冷启动首扫偶发扫不到对端 | 看着像竞态，未定位 —— **重跑一次** |
| 守护进程断线重连 | **没有测试碰过**；长时间跑会不会漏内存也没数据 |
| `slam_toolbox` 的 `map_saver.cpp` | 走 `system("ros2 run ...")`，Windows 上必失败 —— 用 `map_saver_cli.exe` |
| 窄道 / 回环 / 长时间漂移 | 没测 |

---

# English

## Check the state first

```powershell
ros2 bridge status                     :: reads the graph only, triggers no wait
bridge\windows\ros2_bridge.bat status  :: daemon status + log paths
```

> **On a single machine with no Ubuntu**, `ros2 topic list` **starts the daemon on demand** and
> waits for the first sync, with a 30 s budget by default. With no Ubuntu and no relay, nobody is
> listening on port 50060 → it waits until the budget runs out and the tool call hangs.
> **Always turn autostart off when testing locally**:

```powershell
$env:ROS2_BRIDGE_AUTOSTART = '0'    # do not start the daemon by itself
$env:ROS2_BRIDGE_WAIT_MS = '1500'   # if you really want to wait, make it short
```

## "ros2 is not recognized as an internal or external command"

**A `.bat` cannot change a PowerShell session.** A `.bat` runs in a cmd.exe child process; `set`
only affects that child, and the variables die with it. So running a `.bat` in PowerShell prints
`echo %VAR%` correctly (that expansion happens *inside* cmd), but `ros2` is still not on the PATH
— PowerShell's PATH was never touched.

**The correct approach: dot-source the `.ps1`**

```powershell
. E:\DesktopData\app\code\CPU\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

The `.` is essential: **executing** leaves nothing in the session, **dot-sourcing** does. If you
would rather not, `bridge\windows\ros2_shell.bat` opens a PowerShell window that is already
prepared, or run one command at a time with `bridge\windows\ros2.bat topic list -t`.

> **`.ps1` files must be UTF-8 with a BOM.** Windows PowerShell 5.1 decodes a BOM-less `.ps1` as
> ANSI (cp936): the Chinese turns to mojibake, **Chinese file names turn to mojibake → "file not
> found"**, and — more subtly — **the logic breaks too**: `$env:ROS2_BRIDGE_AGENT` once had a
> fallback assignment and was still always empty, because 5.1 had decoded the whole file wrongly.
> Use `python tools/fix_ps1_encoding.py start` to normalise them.

## "`topic list` shows no remote topics"

Work through this in order:

1. **Is the Ubuntu side up?** The other order means Windows finds nothing. Check
   `bash bridge/scripts/ubuntu_sim_ctl.sh status` or `bash start/connector.sh status`.
2. **Discovery ignores this host by default.** When the daemon and the agent share a machine you
   must give the address: `ROS2_BRIDGE_AGENT=127.0.0.1:50060` (or `:50051`).
3. **It waits for a ready file, not a fixed sleep.** Cold start measured at 1.3–2.8 s; if it is
   still not there after the budget, read the daemon log.
4. **A cold-start sweep occasionally misses the peer**: once `topic_list` reported "all remote
   topics missing" after 5.8 s while every other check in that run passed, and a re-run was fine —
   it looks like a race between the first sweep and the peer agent, not localised. **Re-running is
   the cheapest workaround.**

## "Start Coordinates ... was outside bounds" for every goal

You forgot `/sim/reset` before starting the stack. AMCL takes `/odom` as its initial pose, so a car
parked elsewhere produces this, and **restarting the stack does not heal it**.

```bat
ros2 service call /sim/reset std_srvs/srv/Trigger
```

## "`remaining` is 0.00 m, `/plan` ×0, the behavior tree reports SUCCEEDED"

The car is still parked at the previous goal. **Reset before every nav** — the number in
`travelled` is only the previous run's displacement, so it *looks* like it worked while the car
never moved.

## "could not drive /map_server/change_state transition 3"

A `map_server.exe` from the previous run still owns the `/map` writer. **Stop the previous stack
before running `--map`**:

```bat
python bridge\scripts\run_nav2_stack.py stop
```

Also, `/map` may have only one writer: if the daemon is mirroring `/map` it refuses to start and
prints the two commands that remove `/map` from the mirror set.

## "`bt_navigator` will not activate"

The behavior tree is **built at activation**, and if the action server behind a leaf is missing the
whole `bt_navigator` cannot activate — so `lifecycle_manager`'s `node_names` **must list
`behavior_server`**.

## A segfault with no readable error

Plugin registry keys **must be namespace-qualified**. `dwb_critics::PathAlignCritic` and
`mppi::critics::PathAlignCritic` are two different classes; a short-name key lets them overwrite
each other, and the overwritten one is then used as a pointer to **another base class** (the `Base`
of the `static_cast<Base *>` in the factory is the base registered at load time), so the virtual
call jumps into the wrong function.

Lookup order: "namespace-qualified spelling → after `normalize()`", see `detail::lookup_keys()` /
`detail::find_factory()`, with cases in `tests/plugin_names.cpp`.

## "The goal was accepted but the tree does not move"

```powershell
$env:NAV2_SHIM_BT_TRACE = '1'      # bt_navigator prints each node's state periodically
$env:ROS2_SHIM_ACTION_TRACE = '1'  # every send/receive on the six action channels
```

Sort each node's `.log` by time and you can see directly "who sent and who did not receive".

With `ROS2_SHIM_ACTION_TRACE=1` every send also checks `ShmChannel::publish()`'s return value and
prints `[act !! ] publish failed: N bytes > slot_size M` for an oversized payload (this line is
**not** gated by the switch — a dropped message is always a bug).

## "The action caller waits forever"

The client has **no timeout of its own** (matching upstream), so a genuinely lost ack/result shows
up as the caller waiting forever. The `ROS2_SHIM_ACTION_TRACE` above is the diagnostic.

Also, **the action ring buffer has only 8 slots**. `_feedback` runs at ~40 Hz while following, so
the buffer is about 190 ms: any scheduling delay over 190 ms makes the reader skip the feedback in
between. Losing a few feedback messages is harmless, but this "slow readers silently drop
messages" property is inherent to the transport.

## "std::bad_alloc"

Two known sources:

* **Shared-memory geometry**: if `map`/`costmap` topics are reserved as 64 MB × 8 slots, one `map`
  channel takes 512 MB and `planner_server` alone takes 6 GB — which shows up as a
  `std::bad_alloc` in `bt_navigator`. `bridge_env` now uses **512 KB / 2 MB / 64 KB** (total
  ~600 MB). **After changing the geometry you must restart the daemon and every nav2 node
  together** (already-open channels keep the old layout).
* **SMAC's out-of-range angle bin** (fixed). Before the fix: repeatedly planning towards an
  unreachable goal, the first attempt failed normally (~770 ms to `NoValidPath`), and **from the
  second attempt on every run threw `std::bad_alloc` in 47 ms**. A gdb `__cxa_throw` catchpoint
  pinned the allocation site; all four out-of-range spots are in `nav2_smac_planner/`. After the
  fix the same bad goal reliably reports `PlannerTimedOut: "exceeded maximum iterations"`.

## "std::terminate on shutdown"

`Costmap2DROS` owns a `std::unique_ptr<std::thread>` map-update thread joined only in
`on_deactivate()`. If the node is still ACTIVE at shutdown, that member is destroyed before the
base destructor runs, and a joinable thread destroyed without join calls `std::terminate`.

The shim supplies the three missing semantics: **one context per process**,
**`remove_pre_shutdown_callback()` really removes**, and **`rclcpp::shutdown()` runs the callbacks
before clearing the `ok` flag**.

## "`now()` is 56 years away from the message stamps"

**Nodes without `use_sim_time`** have `now()` on the system wall clock (1.79e9 s), while the Ubuntu
simulator stamps messages with **simulation time** (starting at 0). Any code in such a node that
pairs `now()` with a message stamp extrapolates into the future.

The nodes the stack brings up (`amcl` / `planner_server` / `controller_server` / `bt_navigator` /
`behavior_server` / `smoother_server` / `waypoint_follower`) all run with `use_sim_time`.

## "`ros2 topic pub` only reaches 64 Hz"

Windows' default timer granularity is 15.6 ms, so sleeping `1000/rate` **milliseconds** degrades to
~64 Hz. It now advances by deadline (`wait_until()` in `rclcpp/precise_wait.hpp`); measured
1000 Hz → 1002.7 Hz.

> **But** the `rclcpp` executor poll loop still contains a `sleep_for(1ms)`, so **a node's own
> timers** still top out around 64 Hz. nav2's 20–50 Hz is unaffected; kHz-class node timers need
> a different fix.

## "`ros2 topic echo` exits because there is no publisher"

Standard ROS2 **creates the subscription and waits**. This now uses `create_channel()` (the shim's
rule: whichever process gets there first wins) and **resolves the type lazily** while waiting —
`refreshSubscriberType()` fills in the recorded type once a publisher appears.

## `bash` is unusable on Linux

The `bash` on this machine is WSL's `bash.EXE`, but **no WSL distribution is installed**, so
`bash -n xxx.sh` only prints "The Windows Subsystem for Linux has no installed distributions".
Check `.sh` syntax with Python instead (balance keywords: if/fi, for/done, case/esac — and skip
heredoc bodies).

## Writing verification scripts

* **Do not** use `cmd /c '... && echo %VAR%'` — `%VAR%` is expanded **during the whole command
  line parse**, before `call`, so it looks like "the variable did not take effect" when in fact
  the test is wrong. Use delayed expansion `cmd /v:on /c '... & echo !VAR!'`, or more directly:
  dot-source the `.ps1` in PowerShell and read `$env:VAR`.
* **Write test scripts to a file and run them with `-File`**; do not put Chinese paths into
  `powershell -Command` — the bash → powershell command line breaks them apart. Write the file,
  then locate the target with `Get-ChildItem -Filter '*.ps1'` plus an ASCII wildcard, so the
  Chinese path never travels through the command line.
* Verification scripts must **load bridge_env**: use `bridge/scripts/_bridge_env.py` to read the
  variables out of `bridge_env.bat`. **Do not** use `cmd /c "call x.bat && set"` (cmd re-quotes a
  command line containing quotes, which breaks `call "path"`) — use a **temporary `.bat` wrapper**
  (`call ...` + `set`) to sidestep the quoting. Otherwise the daemon does not get the slot
  geometry, falls back to the 2 MB / 8 MB defaults, and memory use goes back up.

## Upstream source must not be changed

Changing anything under `nav2/nav2_extracted/` is caught by `nav2/patches/verify_patch.py`. The
rule is: **don't change it if you can avoid it** (put it in `prelude.hpp` / `include\ros2_shim\`);
if you truly must, express it as a patch in `nav2/patches/` that `verify_patch.py` can verify.

## Known but unlocalised / unproven

Some symptoms are **known to exist but not understood**. Work around them:

| Symptom | State |
| --- | --- |
| A cold-start sweep occasionally misses the peer | Looks like a race, not localised — **re-run** |
| Daemon reconnect after a dropped link | **No test touches it**; whether a long run leaks memory is also unknown |
| slam_toolbox's `map_saver.cpp` | Goes through `system("ros2 run ...")`, always fails on Windows — use `map_saver_cli.exe` |
| Narrow passages / loop closure / long-term drift | Untested |
