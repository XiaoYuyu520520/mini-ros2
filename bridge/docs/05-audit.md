# 05 · 审查（2026-09-17）：离「Windows 上能用的 ros2 + slam + nav2」还差什么

这一份是**审计**，不是设计文档。每一条结论后面都跟着它是怎么量出来的：源码行号、
命令行原样输出、或者一次真跑下来的数字。写它的目的是把「这个项目现在到底能做什么」
从文档里的叙述变回可复核的事实 —— 这个项目最容易自我贴金的地方，是把「Windows 本机
共享内存上能跑」说成「跨机能跑」，把「写在注释里的意图」说成「验证过的行为」。

## 一句话结论

Windows 本机这一侧的 ros2 + slam + nav2 **是能跑通的**（五棵测试树 101 条用例全绿，
`slam_demo` 建出 473x472 的图，nav2 规划 271 个航点、驱动小车走完 11.7 m、终点误差
0.30 m，`acceptance_check.py` 6/6）；跨机那条链路的**边界不是带宽也不是延迟，是编解码表**
—— **99** 个类型过得了桥，第 100 个就不行，因为桥两端各有一张手写的表（下文第一节那张表是
这一轮开始时的 85，action 与 `Image` / `CameraInfo` 都是当晚补的，见证据 61 / 62）。对端（Ubuntu）
并不是 ROS2，是按 proto 手写的 Python 仿真，所以「Windows 能进一个真实的 ROS2 图」
这件事**目前没有任何证据**。测试侧这一轮抓到的最硬的东西是：`py_interop` 与
`test_nav2_ros_common` 这两条测试**编译了很久、但从来没有注册成 ctest**，也就是整个
shim 赖以成立的那条契约（两边说的是同一种 CDR）可以静默烂掉而没人知道；补注册的时候
又发现后者**连一条断言都没有**（只有 `return 0`），所以这一轮把注册和断言一起补了。

## 一、怎么审的

* 测试清单：`ctest --test-dir <树> -N` 与 `--show-only=json-v1`，不用正则去解析
  `CTestTestfile.cmake`（正则会漏掉 `add_subdirectory` 进来的子目录，我第一遍数成
  28 / 3，是错的）。
* 缺口清单：不照抄 04-gaps.md，逐条回到源码或命令行重新量一遍（下面第七节每条都标了
  出处）。
* 活的链路：`slam_demo.bat`、`nav2_demo.bat`、`acceptance_check.py` 这一轮各真跑了一次。
* 编译：`python nav2\ros2_shim\build.py`（只有 6 个大核、钉核，见 02-running 14.1）。

## 二、测试树：29 → 31，补上两条「编了但从来没注册」的测试

### 2.1 缺陷本身

`nav2\ros2_shim\CMakeLists.txt` 里 `add_executable` 有 33 个，`add_test` 只有 29 条
（都在 1017-1056 行）。逐条对下来，有两条**有可执行文件、有源码、有 `main`，就是没有
`add_test`**：

| 目标 | 源码 | 原来的状态 |
|---|---|---|
| `test_nav2_ros_common` | `tests/test_nav2_ros_common.cpp` | 编了，从不执行 |
| `py_interop` | `tests/py_interop.cpp` + `python/py_interop.py` | 编了，从不执行 |

`test_nav2_ros_common` 尤其说明问题：03-validation 的证据 44 里那张「34 条测试逐条落表」
的表，把「`test_nav2_ros_common` 仍然是唯一『名叫 test 却从不执行』的」写成了结论，
然后它就一直是结论 —— **一条已知的洞被记录下来，就等于被处理掉了**。这一轮把它补上。

`py_interop` 更值钱：它是 C++ 实现与 Python 实现在**同一份 CDR** 上的四路对照，
是这个项目最底层的那条假设。它四路都写在源码里（`tests/py_interop.cpp:103-152`、
`python/py_interop.py`），但从 C++ 侧看它只是四个模式，没有一个「把四路跑完」的入口，
所以谁也没法把它挂成一条 ctest。补法是给它写一个 Python 驱动。

### 2.2 补法

`nav2\ros2_shim\CMakeLists.txt`（两处插入）：

```cmake
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME ros2_shim_py_interop COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/interop_matrix.py
    --cxx $<TARGET_FILE:py_interop>
    --tmp ${CMAKE_CURRENT_BINARY_DIR}/interop)
else()
  message(STATUS "ros2_shim: no Python3 interpreter, ros2_shim_py_interop not registered")
endif()
```

```cmake
add_test(NAME ros2_shim_nav2_ros_common COMMAND test_nav2_ros_common)
```

`QUIET` 是刻意的：树里除这一条之外没有别的地方依赖 Python 解释器，不该让一个
「本机没装 Python」把整个 configure 打断；找不到就打印一行 STATUS，
**安静的跳过比假装通过好，比配不出来也好**。`$<TARGET_FILE:...>` 而不是写死
`py_interop.exe`，所以它跟着构建类型与目录走。

新增文件 `nav2\ros2_shim\python\interop_matrix.py`：五条腿的驱动。

### 2.3 五条腿（实测，不是设计）

```
cross-language interop matrix (slot_size=4096)
  1  cpp dump -> py check   PASS  cpp dumped 303 bytes, py check rc=0
  2  py dump  -> cpp check  PASS  py dumped 303 bytes, cpp check rc=0
  3  byte identity          PASS  cpp 303 bytes, py 303 bytes, identical=True
  4  cpp pub  -> py sub     PASS  py sub read 303 bytes / 4 poses, cpp pub rc=0
  5  py pub   -> cpp sub    PASS  py pub sent 5, cpp sub rc=0 cpp sub: OK (303 bytes, 4 poses)
interop matrix: 5/5 legs passed
```

前三条是上一轮手工跑过的（第 3 条是新增的：两份 dump **逐字节相同**，而不只是
「互相读得懂」—— 后者挡不住两边各写了一套等价但不同的编码）。**第 5 条 `py pub -> cpp sub`
是这一轮第一次跑**：以前只验了 C++ 写、Python 读这一向。反向能过，说明那条环的
三个部分（头部布局、槽索引、写者/读者游标）在 ctypes 侧与 C++ 侧完全对得上，
不只是「Python 能读 C++ 写的」。

挂成 ctest 之后：

```
Test #3: ros2_shim_py_interop .............   Passed    3.39 sec
Test #7: ros2_shim_nav2_ros_common ........   Passed    0.04 sec
```

### 2.4 顺手修掉的一个缺陷

第一次跑驱动时，重试 `ShmChannel.open()` 的过程中 `__del__` 报了一条
`AttributeError: 'ShmChannel' object has no attribute '_handle'`：`__init__` 先检查
`handle`、后赋值，打开失败时抛出的那个 **RuntimeError 已经够清楚了**，紧接着
`__del__` 在半成品对象上又炸一次，把一条清晰的错误变成两条（`nav2\shm_transport\python\shm_transport.py`）。
改成先把 `self._handle = None` 放上去再检查。

## 三、这一轮跑过的五棵树（其中一棵是我第一遍漏掉的）

| 树 | 条数 | 结果 | 用时 |
|---|---|---|---|
| `nav2\ros2_shim\build` | **31**（29 → 31） | 31/31 | 52.7 s |
| `bridge\windows\build` | 6 | 6/6 | 3.6 s |

（`bridge\windows\build` 那一行 2026-09-18 按当天的树重新数过：`bridge_cli_values` 加进来之后
是 5 条，把一直没注册的 `bridge_shm_roundtrip` 补上之后是 6 条 —— 其中 `shm_transport` 是
`add_subdirectory(nav2/shm_transport)` 带进来的，所以它和上面那一行是同一个用例。
同一天复跑另外四棵：`shim` **32/32**、`shm` **1/1**、`gRPC` **4/4**、`sim` **71 passed**。
| `gRPC\ros2_gRPC_sdk\build` | 4 | 4/4 | 1.7 s |
| `nav2\shm_transport\build` | 1 | 1/1 | 1.1 s |
| `bridge\ubuntu\ros2_bridge_sim\tests`（pytest，在 Windows 上跑） | 61 | 61 passed | 21.3 s |

**第一遍漏了 gRPC SDK 那 4 条**（`localhost_e2e`、`services_localhost`、
`discovery_localhost`、`config_localhost`，1.68 s 全绿）：`ctest -N` 得一棵树一棵树去问，
而「这个项目的测试」在我脑子里默认是 nav2 侧那一棵。它们是在**守护进程正在跑**的情况下
过的 —— 也就是说这两套东西不抢端口，不是「挑了个没人的时候才过」。这是这一轮审计的
第一个自我纠正：**报告里最容易错的不是结论，是那个被当成全集的小集合**
（顺带一提，`gRPC\ros2_gRPC_sdk\build` 这一棵，是「基于 gRPC 的通信 SDK」这半个项目的
全部测试面：4 条，全部是 localhost 单机，**没有一条跨机**）。

### 3.1 隔离性：把守护进程停掉再跑一遍

`nav2\ros2_shim` 的 31 条**在两种环境下都全绿**：

| 环境 | 结果 | 用时 |
|---|---|---|
| 守护进程在跑 + Ubuntu 仿真在跑 | 31/31 | 52.72 s |
| **守护进程停掉**（`ros2_bridge.bat stop`） | **31/31** | 53.04 s |

第二行才是真正有信息量的那一行：它排除了「测试之所以过，是因为桥上正好有活的数据在
喂它」这种解释。每条 ctest 自带通道命名空间（证据 20），这一条现在**是有实测支撑的**，
不是设计意图。

### 3.2 并行重复性：`-j 8` 五轮

历史结论是「十轮里 7 轮干净」（02-running 14）。现在（31 条，含新的 `ros2_shim_py_interop`）：

```
ctest -j 8, 5 rounds, pinned to cores 0,1,10,11,12,13 (mask 0x3C03)
[  1/5]   21.6s  ok  100% tests passed out of 31
[  2/5]   20.6s  ok  100% tests passed out of 31
[  3/5]   22.8s  ok  100% tests passed out of 31
[  4/5]   21.8s  ok  100% tests passed out of 31
[  5/5]   19.5s  ok  100% tests passed out of 31
5/5 rounds clean, 0 rounds with a failed test
```

5 轮干净、每轮 19.5-22.8 s（比串行快一倍多）。这不推翻历史那三种失败形状（成因分析在
证据 42 / 43），只说明**今天这 5 轮里一种都没出现**；要下「稳定」的结论得上 50 轮那个
量级，而这一轮没跑。

把 `test_nav2_ros_common` 改成真断言之后又跑了一遍：串行 **31/31、56.41 s**，`-j 8`
**3/3 干净、18.7-23.1 s**。这一轮一共 8 轮并行，8 轮干净。

### 3.3 断言普查：注册了不等于在测东西

先用大写正则扫（`CHECK|REQUIRE|EXPECT|ASSERT`）：33 个测试源里**只有 5 个**有断言。
这个数字是错的 —— 证据 44 早就写过「这棵树有两套断言命名，只认大写就会误判」，
而我正好就是这么干的。这棵树的两个约定是：

* **大写宏** `CHECK(...)`：`lifecycle_services.cpp`、`navfn_planning.cpp`、`test_e2e.cpp`、
  `test_messages.cpp`、`test_shim_node.cpp`；
* **小写 helper** `check(bool, what)` / `expect(bool, what)` 配一个 `failures` 计数器，
  结尾打 `PASSED/FAILED` 再 `return failures == 0 ? 0 : 1`：其余 21 个。

取并集之后，**31 条 ctest 里运行期一条断言都没有的只有三条**：

| ctest | 源文件 | 性质 |
|---|---|---|
| `ros2_shim_compile_check` | `compile_all_messages.cpp` | 编译期检查：编不过就红，运行期本来无事可做 |
| `ros2_shim_compile` | `test_shim_compile.cpp` | 同上 |
| **`ros2_shim_nav2_ros_common`** | `test_nav2_ros_common.cpp` | **只有 `return 0`** —— 不崩就算过 |

第三条就是这一轮刚注册的那一条。也就是说「补一行 `add_test`」只完成了一半：它进了 ctest，
但它在测的仍然是「能不能编、会不会崩」。同一个文件现在改成 **15 条断言**（名字、命名空间、
`StandardTopicQoS` 的三个字段、图里的发布者与订阅者计数、四个生命周期状态、`configure()`
的返回值就是节点当前所在的状态、两个回调各跑一次），并且验过它**真的会红** ——
把 `count_publishers("topic") >= 1` 改成 `>= 99`、单独编一份出来跑：

```
FAILED (1 failure(s))
mutant exit code = 1 (expected 1)
```

一条不会失败的测试比没有测试更坏：它会往「31 条全绿」里掺水。

### 3.4 跑完留下什么：进程干净，`%TEMP%` 不干净

四棵树跑完，`Get-Process` 里只剩守护进程一个 —— **没有进程泄漏**。但 `%TEMP%` 会一直涨：

| 残留 | 数量 | 大小 |
|---|---|---|
| `nav2_shim_graph_<命名空间>` 目录 | 37（其中 **34 个是空的**） | 0 |
| `acceptance-*.pgm` / `*.posegraph` / `*.log` | 40 | **43.8 MB**（单个 `-reload.posegraph` 7.0 MB） |
| 早期探针留下的 `<pid>.graph.tmp` | 2 个目录 | 几十字节 |

每个通道命名空间会在 `%TEMP%` 里留一个目录，空目录也不删（`ctest_*`、`audit_*`、`probe_*`、
`manual_serial`、`rosoutdev` …）；每一轮验收又会在 `%TEMP%` 里新留一份 7 MB 的位姿图 +
200 KB 的 PGM，**没有任何东西清理它们**。这不是 bug，是一条「跑得越多、机器越脏」的记录：
`acceptance_check.py` 把中间产物直接写在 `%TEMP%` 而不是自己的临时目录里，是它可以顺手
改掉的一处。

## 四、实跑：SLAM 能用吗 —— 能

`slam_demo.bat` 先跑了一次「原地转一圈」（我发的是一条 `angular.z=0.6` 的 `/cmd_vel`，
靠仿真「保持最后一条命令」的特性转满 10.5 s）：

```
MAPDUMP frames=3 size=473x472 res=0.0500 origin=(-11.733, -11.800)
        occupied=42 free=1651 unknown=221563
```

再跑一次正式验收里的那一趟（4 x 「走 2 m + 转 90°」的闭环）：

```
[PASS] slam (83.1 s) - 472x473 @ 0.050 m, 280 occupied / 41068 free / 181908 unknown;
       reloaded in a fresh process: 473x473, 426 occupied
```

也就是 `/scan` + `/tf` 从共享内存进来、slam_toolbox 在 Windows 上建图、`/map` 发出去，
以及**存档 → 换一个进程用 `--mode localization` 读回来**（426 个占据格）这一整条都是通的。

**第一次跑出来是一张空图**（`size=8x5 全 unknown`），原因见第六节 —— 不是 SLAM 的问题，
是车已经开出房间 100 m，雷达什么都看不到。这件事本身值得记：**一张「能发出来但内容
是空的」的图，和一套「没坏」的建图链路，在日志上长得一模一样**。

SLAM 缺的是周边，不是核心（第七节 7.5）。

## 五、实跑：nav2 建图 + 规划 + 移动

`nav2_demo.bat --goal auto`：

```
  received   : /map x1 (480x480 @ 0.050 m), /scan x8 (360 beams), /odom x38
  costmap    : 480x480, 11596 lethal, 13159 inscribed, 0 unknown cells
  auto goal  : picked (10.43, 6.83), 12.00 m away, 1 of 2017 candidates tried
  plan       : 271 waypoints, 14.51 m long, from (-0.42, 1.73) to (10.43, 6.83)
  driving    : 10 Hz control loop, 0.60 m/s limit, replanning every 3.0 s
  reached    : (10.16, 6.69) after 22.1 s, 11.70 m from the start, 0.30 m from the goal
  result     : OK
```

`acceptance_check.py` 全跑（6/6）：

| 检查 | 结果 |
|---|---|
| `topic_list` | 冷启动（先杀守护进程）2.0 s 列出 7 条话题带类型 |
| `topic_info` | `/scan` 报告为 remote（bridged over gRPC） |
| `scan_rate` | 10 Hz 的雷达镜像过来 9.52 Hz（54 个样本） |
| `uplink` | Windows 发 `/cmd_vel`，Ubuntu 的车动了 0.62 m，`/odom` 的 `twist.linear.x=0.250` |
| `nav2_demo` | 目标 `[-1.175, 9.625]`，从 `[10.5869, 7.25984]` 出发，走 11.71 m，剩 0.29 m，计划 258 个航点 |
| `slam` | 见第四节 |

## 六、这一轮的新发现：仿真底座没有 `cmd_vel` 看门狗

接手时车正以 0.2 m/s 前进（`/odom` 的 `pose.position.x=95.98`，`twist.linear.x=0.2`），
而我这一轮**没有发过任何速度命令**。四秒后再看：

```
t0: pose.pose.position: {x: 100.892, y: 0, z: 0}
t4: pose.pose.position: {x: 101.704, y: 0, z: 0}      # +0.812 m / 4 s = 0.203 m/s
```

发一条 0 之后：

```
after stop t0: {x: 102.52, ...}
after stop t4: {x: 102.52, ...}
```

`bridge\ubuntu\ros2_bridge_sim\sim.py:102-117`：`_on_cmd_vel` 只是把值存进
`self.linear/self.angular`，积分循环永远用它，**没有任何超时**。也就是说：

* 「小车在动」不构成任何证据 —— 上一次 demo 留下的最后一条命令会让它一直动下去，
  我的第一次 SLAM 就是这样在房间外建了一张 8x5 的空图；
* 真实部署里挡这件事的是 `velocity_smoother` 的 `cmd_vel_timeout` 与
  `collision_monitor`，**仿真底座这一层没有**，所以整条链路上没有任何东西会发现
  「发布者已经死了，车还在跑」。

规避（不用改代码）：跑任何 demo 之前先发一条 0，或者用 `/sim/reset`
（`acceptance_check.py` 收尾时就会 reset，这也是它为什么可靠）。真正的修法是给
`set_cmd` 加一个 watchdog 参数，让「多久没收到命令就归零」变成一个可配的仿真参数 ——
毕竟这是一个**要给 nav2 做验收靶子**的仿真，它的底座行为应该像一台会被 nav2 的
安全链保护的真车，而不是像一台「最后一条命令永生」的理想积分器。

顺带记一条**不是缺陷**的行为：`ros2 topic pub` 不带 `--once`/`--duration` 时和上游一样
会一直发下去（我第一次就这么挂住了 shell）；它支持 `--once` / `--rate` / `--duration`，
而且在 `--once` 时若还没有人持有话题会**警告可能丢失**（`bridge\windows\tools\ros2_cli.cpp:659`）。
（2026-09-17 夜这一段又往后挪了 74 行：三个子命令的取值换成真 YAML 时顺手补上了
`-1` / `-r` / `-t` / `-p`，见证据 64。）

## 七、缺口清单复核（逐条重新量）

### 7.1 跨机编解码表：38 / 38 / 20

| 表 | 出处 | 条数 |
|---|---|---|
| 话题 | `bridge\windows\src\type_registry.cpp:814-874` | 40（2026-09-17 深夜加 `Image` / `CameraInfo`，证据 62） |
| service | `bridge\windows\src\service_registry.cpp:770-911` | 38 |
| action | `bridge\windows\src\action_registry.cpp` 的 `registry()` | 20（2026-09-17 深夜补满，证据 61） |

和历史文档一致（service 那一栏 2026-09-17 从 21 涨到 30、再涨到 38，见第十五 / 十六节；
action 那一栏同一天深夜从 9 涨到 20，见证据 61；话题那一栏又在同一天深夜从 38 涨到 40，见证据 62）。
表里的 **99** 个类型就是**跨机能力的全部**：`ros2 topic echo` 能读懂
什么、`ros2 topic pub` 能构造什么、service call 认得哪些类型，全都由它决定。
不在表里的东西不是「还没调好」，是**根本过不来**。几个具体的：

* `sensor_msgs/msg/Image` 不在表里 → `image_transport` / 相机这一类话题没有任何通路；
* ~~slam_toolbox 的 11 个 srv 里只有 `SerializePoseGraph` / `DeserializePoseGraph` 在表里，
  另外 9 个（`AddSubmap` `Clear` `ClearQueue` `LoopClosure` `MergeMaps` `Pause` `Reset`
  `SaveMap` `ToggleInteractive`）过不来~~ —— **2026-09-17 补全**：11 个全在表里，外加
  `nav_msgs/srv/GetMap`（`slam_toolbox/dynamic_map` 用的就是它，一并补了），见第十四节；
* 带数组字段的 goal 发不出去（`build_goal` 明确拒绝），所以能跨机发 goal 的 action
  是「标量字段的那些」。

### 7.2 兼容层表面与 CLI

`ros2.bat --help` 的完整命令面（原样抄下来的）：`topic list|info|type|echo|hz|pub|find`、
`service list|type|find|call`、`action list|type|find|send_goal|cancel`、
`param list|get|set|describe|dump`、`node list`、`interface list`、`bridge status`。
**没有**：`bag`、`launch`、`run`、`lifecycle`、`node info`、`interface show`、
`topic bw`、`topic delay`。

`ros2 interface list` = **99** 个类型（2026-09-19 命令行数出来的：**41 msg** + 38 srv + **20 action**；
同一天早些时候是 85，action 与 `Image` / `CameraInfo` 都是当晚补的，见证据 61 / 62）。
其中 msg + srv + action 三张表就是跨机能力的全部，表外的类型不是「还没调好」，是根本过不来。

兼容层层面缺的还是这些（与历史一致，未变）：`rclpy`、`rosbag2`、`launch`/`launch_ros`、
`image_transport`、RViz/rqt、tf 命令行工具（`static_transform_publisher` / `tf2_echo` /
`tf2_monitor`）、component container（`rclcpp_components` 只有注册宏）。

### 7.3 对端不是 ROS2（这一条最容易被忽略）

三条都空：

```
no ros2 on PATH
no /opt/ros
ModuleNotFoundError: No module named 'rclpy'
```

Windows 侧那个「能变成真 ROS2」的开关也确实从来没打开过：

* `gRPC\ros2_gRPC_sdk\CMakeLists.txt:8` → `option(ROS2_GRPC_SDK_WITH_ROS2 ... OFF)`；
* `gRPC\ros2_gRPC_sdk\build` 整棵树里**没有任何 `rclcpp_backend` 产物**
  （`.obj` / `.a` / `.exe` 都没有）。

所以「Windows 端能进一个真实 ROS2 图」这件事，**没有证据**，而且不用去怀疑它有没有
跑通 —— 它连一次都没被编译过。要做这件事，最小的第一步是找一台装了 ROS2 的机器把
`-DROS2_GRPC_SDK_WITH_ROS2=ON` 编出来（`gRPC\ros2_gRPC_sdk\scripts\build.py:304` 已经
支持这个开关）。

### 7.4 nav2 包：vendored 34 个，真正缺 7 个

`nav2\nav2_extracted\navigation2-1.5.1` 下有 34 个带 `package.xml` 的包；
`ros2_shim\CMakeLists.txt` 里 `nav2_port_package()` 出现 25 次（含 dwb 的 5 个子库、
docking 的 2 个）。**从头到尾一次都没被构建链提到**的有 8 个：

`nav2_theta_star_planner`、`nav2_constrained_smoother`、`nav2_loopback_sim`、
`nav2_rviz_plugins`、`nav2_simple_commander`、`nav2_system_tests`、`nav2_common`、
`nav2_msgs`。

其中后两个不是「缺功能」：`nav2_common` 是构建工具，`nav2_msgs` 用的是**生成好的头**
（`nav2_msgs/action/NavigateToPose` 这些在 action 表里）。所以真正的功能缺口是 6 个，
再加上**连源码都没有 vendored** 的 `nav2_following` = **7 个**，与 04-gaps 的结论一致。

这 7 个里只有 `nav2_theta_star_planner`（换个全局规划器）与 `nav2_simple_commander`
（Python 的 `BasicNavigator`，但它依赖 `rclpy`，而 `rclpy` 根本没有）是真会被用到的；
`rviz_plugins` 与 `system_tests` 是可视化 / 上游自测，`loopback_sim` 是被这个项目
自己的仿真替代掉的东西。

### 7.5 SLAM 周边

slam_toolbox 本体是能用的（第四节）。缺的是：

* **求解器**：插件名还叫 `CeresSolver`，实现是自写的 `EigenPoseGraphSolver`
  （`[INFO] Using solver plugin solver_plugins::CeresSolver` 紧接着
  `EigenPoseGraphSolver: configured with 50 max iterations`）—— 名字留着是为了不改编
  slam_toolbox 的插件选择逻辑，**不是「移植了 Ceres」**；
* 9 条 srv 过不来（7.1）；
* **跨机存档**：Ubuntu 建图 → 拷到 Windows 定位，这一条没做（本机存档 → 换进程定位
  已经做了）；
* `sensor_msgs/Image` / `image_transport` 在表外，所以纯视觉的建图路线不通。

### 7.6 工程化

顶层没有 git 仓库，所以没有 diff、没有 blame、没有 CI（这一轮我改了 4 个文件，
是用备份 + 断言替换条数来保证没改坏的，这不应该是常态）。守护进程的日志没有上限，
已经涨到 50 MB / 56 万行这个量级（**已修**：轮转 + 上限，见第十二节；这条写的是当时的样子）。
`ros2_shim_graph_rewrite` 的**跨进程**那一半也**已经关上**了（见第十三节）。

### 7.7 兼容层的一处**潜在**拼法缺口：QoS 的枚举名

给 `test_nav2_ros_common` 写断言时我写了 `rclcpp::ReliabilityPolicy::Reliable`，**编不过**：
shim 这三个枚举用的是 RMW 的老拼法（`nav2\ros2_shim\include\rclcpp\qos.hpp:83-109`）：

```cpp
enum class ReliabilityPolicy { SYSTEM_DEFAULT, RELIABLE, BEST_EFFORT };
enum class DurabilityPolicy  { SYSTEM_DEFAULT, TRANSIENT_LOCAL, VOLATILE };
enum class HistoryPolicy     { SYSTEM_DEFAULT, KEEP_LAST, KEEP_ALL };
```

nav2 1.5.1 自己的源码里**没有**用到这些拼法（全仓库搜 `::RELIABLE` / `::Reliable`，只有
shim 内部两处和我那份测试），所以它今天不挡任何东西；但任何照新拼法写的代码拿到这个 shim
上就是编译错误。加别名是纯增量的，但要确认该加哪个拼法，得对着一个**真的 rclcpp 头**看，
而这台机器上没有 ROS2（7.3）—— 所以这一条按「潜在缺口」记，不按「已验证的缺口」记。

## 八、按「现在挡着什么」排序

1. **一个 git 仓库**（没有它，上面每一条修复都无法回退、无法评审）；
2. **给仿真底座加 cmd_vel watchdog**（第六节）—— 它现在能让「验收通过」这件事变得
   不可信；
3. **真 ROS2 对端**：一台装了 ROS2 的机器 + `-DROS2_GRPC_SDK_WITH_ROS2=ON`，把
   「bridge 能不能进真实 ROS2 图」从没有证据变成有证据；
4. **编解码表的长尾**：数组字段的 goal、`sensor_msgs/Image`、slam_toolbox 的 9 个 srv —
   每一个都是「加一个表项 + 一组 fixture（`bridge\tests\fixtures`，现在是 35 个类型）」
   的体力活，不是设计问题；
5. **`rosbag2` / `rclpy`**：没有它们，任何「用 Python 写个节点、录一段包回放」的
   常规 ROS2 工作流都不成立；
6. 跨机存档（Ubuntu 建图 → Windows 定位）、`/diagnostics` 的写者
   （要移植 `diagnostic_aggregator`）。~~`ros2_shim_graph_rewrite` 的跨进程那一半~~ ——
   **已关上**，见第十三节。

## 九、这一轮没有做到的

* 没有在**真 ROS2** 上验过任何东西（7.3：这条路连编译都没发生过）；
* 没有跑 50 轮那个量级的并行压力循环（这一轮 8 轮 `-j 8` 全干净，见 3.2），所以「并行下
  稳定」仍然只有 8 轮的证据；
* 断言普查是**结构性**扫描（两条正则取并集 + 人读），不是逐条评审「这条断言够不够强」——
  我只对 `ros2_shim_nav2_ros_common` 一条做过变异验证（3.3）；
* 没有改仿真底座（第六节只给了处方与规避手法，没动 `sim.py`）；
* `py_interop` 那条 ctest 依赖解释器：没装 Python 的机器上它会**不注册**而不是失败，
  这是一次刻意的取舍，但它意味着「31 条」在别人机器上可能是 30 条。

## 十、这一轮之后：五项全部落地，以及它顺手撞出的两件事（2026-09-17 深夜）

第八节那份「按现在挡着什么排序」的清单里，第 2 条和第 4 条的一半做掉了，3.4 / 5 / 7.4 / 7.6
点名的卫生问题一起收了：

| # | 做了什么 | 证据 |
| --- | --- | --- |
| 1 | `sim.py` 的 `cmd_vel` 看门狗：`DEFAULT_CMD_TIMEOUT = 0.5`，`check_cmd_watchdog()` 在 `step()` 最前面跑（暂停期间也跑 —— 暂停时过期的命令必须在 resume 之前销毁）；`/sim/drive`、`/sim/spin` 这类行为循环用 `hold_cmd()` 明说「这条命令就是要一直发」 | 新增 7 条 pytest；`python -m pytest bridge/ubuntu/ros2_bridge_sim/tests -q` = **69 passed**（原 61） |
| 2 | 守护进程日志按大小轮转：`ROS2_BRIDGE_LOG_MAX`（默认 16 MiB，`--log-max-bytes` 可改），一代 `.1`；`--detach` 不占调用者 stdout 的语义没动（只在 detached 时检查）。**2026-09-17 深夜补一处**：当时这个检查只挂在控制循环上，
「连不上 agent、一直停在扫描重试里」那段没有它，日志照样无界增长（证据 63） | 启动时把 68 397 379 字节的旧日志轮转成 `.1`；`ROS2_BRIDGE_LOG_MAX=2000` 下 25 s 内 `4291 → 2211 → 4291 → 2211` 的锯齿 |
| 3 | slam_toolbox 的析构改动入册：`slam_toolbox-thread-shutdown.patch` + `verify_patch.py` 的**覆盖检查**（有改动却没有 patch、一个文件落在两份 patch 里，都报错）+ `slam_toolbox_port/CMakeLists.txt` 的 configure 期门控 | 两份 patch `git apply -p1 --check` 通过、apply 后与 vendor 树逐字节相同；临时删掉 `PORT (ros2_shim)` 标记 → configure 立刻失败并点名该文件 |
| 4 | `tools\README.md` 补 `_toolchain.py` / `check_cross_platform.py`；`run_ctest.py` 认第五棵（pytest）树 | `python tools\run_ctest.py --list` 五行全 ok；`run_ctest.py sim -k watchdog` = 5 passed |
| 5 | `acceptance_check.py` 每个检查一个自己的 `mkdtemp` 目录：**通过就删，失败就留在报告里点名**（那里面那份节点日志是唯一的事后线索） | `%TEMP%` 里积下的 **52 个条目 / 58.28 MB** 清掉；重跑之后 `glob` 出来是空的 |

### 一、它撞出的第一件事：验收会在一个**过期的仿真**上通过

第六节开出来的处方是「给 `set_cmd` 加看门狗」。加完、Windows 侧 69 条 pytest 全绿，新加的那条
检查第一次真跑却是 FAIL：

```
[FAIL] cmd_vel_watchdog (4.6 s) - the robot kept driving after the command stopped:
       drove 0.62 m on a 1 s command, then 0.300 m in the next second, /odom twist.linear.x=0.25
```

看门狗没写错 —— 是 **Ubuntu 上那份 `sim.py` 还是 9 月 14 日的**：`stat -c %y` 给
`2026-09-14 12:32`，`grep -c watchdog` 是 **0**。`bridge\ubuntu\ros2_bridge_sim` 的改动要有人
推过去、有人把那个进程重启才算数：

```
python bridge\scripts\sync_to_ubuntu.py --no-venv
tmux kill-session -t sim
tmux new-session -d -s sim -c ~/code/CPU/bridge/ubuntu \
    'exec ~/code/CPU/.venv/bin/python -m ros2_bridge_sim.run_sim --seed 7 --no-console > /tmp/sim.log 2>&1'
```

推过去并重启之后，同一条检查 PASS（`drove 0.38 m on a 1 s command, then 0.000 m in the next
second`）。**这是这一轮最值钱的一条**：Windows 侧的绿说明不了远端在跑什么代码，
`sync_to_ubuntu.py` + 重启那一步才是「验收通过」开始可信的地方。

### 二、第二件事：看门狗装上之后，`uplink` 那条检查当场变成错的

```
[FAIL] uplink (3.8 s) - published /cmd_vel did not reach the simulator:
       moved 0.59 m, /odom twist.linear.x=0.000
```

`check_uplink` 读 `/odom` 的 `twist` 是在 `topic pub` **返回之后** —— 那时候看门狗早就归零了。
也就是说这条断言过去之所以是绿的，靠的正是第六节要修的那个 bug：最后一条命令永远留着。
现在发布走后台，在命令还在发的时候取一次 twist（报告里的 `odom_twist_while_commanded`），
再等 0.6 s 读落点；`moved > 0.15` 那条断言一个字没动。

### 三、验收现在是 7 条

| # | 检查 | 看什么 |
| --- | --- | --- |
| 1 | `topic_list` | 冷启动后 `ros2.bat topic list -t` 列出 7 条话题及其类型 |
| 2 | `topic_info` | `/scan` 被标成 `remote`（走 gRPC，不是本机） |
| 3 | `scan_rate` | 镜像的 `/scan` 保持 10 Hz（实测 9.45 Hz / 54 个采样） |
| 4 | `uplink` | Windows 发 `/cmd_vel`，车走 0.80 m，且 `/odom` 在命令还在发时报 0.250 |
| 5 | `cmd_vel_watchdog` | 命令停了车也停（第六节那条处方） |
| 6 | `nav2_demo` | nav2 建代价地图、规划、把车开到目标（11.74 m / 267 个路径点 / 剩 0.27 m） |
| 7 | `slam` | 四段「2 m + 90°」跑一圈，存档、再用另一个进程 `--mode localization` 读回来 |

全跑 **7/7**（`.scratch\acceptance_round13.json`）：SLAM 那条 473x474 @ 0.050 m、419 个占据格，
换进程读回来 497 个占据格。`--skip-slam` 是 6/6。

### 四、这一轮之后仍然开着的

* **顶层仍然不是 git 仓库**（第八节第 1 条）：`.github\workflows\cross-platform.yml` 和
  `tools\check_cross_platform.py` 都在，四条树的构建 + ctest 这一轮实测**两个平台都是绿的**
  （`[check] both halves green (6 steps)`）；但那份 workflow **一次 runner 都没跑过** ——
  没有远端可推。CI 里 linux job 那串 apt 包也仍然只是照着这台机器的 `dpkg -l` 抄的。
* ~~`bridge\linux` 仍然没有对应 `ros2_bridge.bat` 的启停脚本~~ **已补上**：四个脚本与
  `bridge\windows\*.bat` 一对一，已在 Ubuntu 上跑通整条生命周期并进了 `check_cross_platform.py`
  的门禁（`04-gaps.md` 第十四节）。
* 第八节的第 3 / 4 / 5 / 6 条 —— 真 ROS2 对端、编解码表长尾、`rosbag2` / `rclpy`、跨机存档 ——
  一条没动，理由与证据见 `04-gaps.md`。

## 十一、三套老 PowerShell 入口改成外壳（2026-09-17 深夜）

`nav2\ros2_shim` 的三个 `.ps1` 是最后一批自己回答「工具链在哪、核在哪、解释器在哪」的文件：
`build.ps1` 写死 MSYS2 根目录与核列表；`run_tests.ps1` 手敲 8 个 `.exe` 加 5 段 py_interop，跑的是
30 条 ctest 的子集，而且**看不见另一棵树里的 `shm_transport`**；`generate.ps1` 写死一个虚拟环境的
`python.exe`。现在三个都只是外壳，逻辑各自只有一份。

| 入口 | 现在 | 实测 |
| --- | --- | --- |
| `build.ps1` | 转给 `build.py`：`-Msys2` / `-BuildType` / `-Jobs` / `-Cores` / `-NoPin` 转成对应参数或 `BIG6_CORES` / `BIG6_NO_PIN` | 退出码 0，最后 12 个目标重新链接 |
| `run_tests.ps1` | 转给 `tools\run_ctest.py shim`，再跑一次 `shm`；多余参数转给 ctest | **31/31 + 1/1**，55.2 s / 1.1 s，末尾 `=== ALL TEST STEPS PASSED ===` |
| `generate.ps1` | 只换掉解释器来源，`--root` 扫描顺序仍是这个文件的重点 | 442 个定义重生成后，`generated/` 320 个文件与 `python/` 394 个文件**逐字节不变** |

解释器一律按 `-Python` → `$env:PYTHON` → PATH 上第一个 `python` 解析，三个文件里都没有本机路径。

同一次清扫还找出三处同类残留，其中两处是真缺陷：

* `nav2\ros2_shim\tools\make_ros_header_aliases.py` 的 `SHIM` 是这台机器检出目录的绝对路径。
  在这台机器上它一直是对的，所以没人注意；换一棵树、或者在 Ubuntu 上，它会**静默什么都不做** ——
  四个包全部 `skip missing package`，然后退出 0。现在从 `__file__` 定位：`wrote 0 alias headers,
  20 already present`，include 树 123 个文件不变。
* `bridge\windows\bridge_env.bat` 只认 `ROS2_BRIDGE_MSYS2`，不认 python 侧统一用的
  `CPU_MSYS2_ROOT`（`tools\_toolchain.py` 读的就是后者）。现在两者都认：实测
  `set CPU_MSYS2_ROOT=D:\somewhere\MSYS2` → `ROS2_BRIDGE_MSYS2=D:\somewhere\MSYS2`，不设时回落到
  本机默认；改完 `ros2_bridge.bat status` 仍然是 7 条远程话题、退出码 0。
* `bridge\windows\ros2.bat` 的用法提示把检出目录打印出来。改成 `%~dp0ros2.ps1`：打印内容不变，
  仓库换地方也不会指错。

**这条证据不能证明**：三个 `.ps1` 外壳没有在 Ubuntu 上跑过（那边没有 PowerShell）。它证明的是外壳
不再自带路径 —— 而它们转发的 `build.py` / `run_ctest.py` 本来就两边都跑过（第四节、第十四节）。

## 十二、P0/P1/P2 逐条重验（2026-09-17 深夜，第二条）

这一节不引入新改动，只把目标清单上的每一条重新对着现场跑一遍 —— 前面几节记的是"当时跑过"，
而第十一节那批改动（三个 `.ps1` 外壳、三处硬编码路径）正好落在同一批文件上，所以值得重验。

| 条目 | 证据（本轮现场） | 结果 |
| --- | --- | --- |
| P0 日志无轮转 | 停掉守护进程，用 `ROS2_BRIDGE_LOG=%TEMP%\rot-test.log` + `ROS2_BRIDGE_LOG_MAX=8192` 起一次：22 s 后出现 `rot-test.log.1`（8402 B），live 文件 4269 → 8429 → 2189（又轮转了一次），全程不超过 2×cap；测试文件删净、默认守护进程已恢复（7 条远程话题） | ✅ |
| P0 仿真车无 `cmd_vel` 看门狗 | `python tools\run_ctest.py sim`：**69 passed in 24.62 s**（`test_sim.py` 里 23 处 watchdog / hold_cmd）；验收里那条：`drove 0.37 m on a 1 s command, then 0.000 m in the next second, /odom twist.linear.x=0` | ✅ |
| P1 析构改动未入册 | `verify_patch.py --upstream .scratch\upstream`：`markers present in all 3 files`、`coverage all 3 edited files are in exactly one of the 2 patches`、两份 patch **与 2.8.5 逐字节一致**；再把 `slam_toolbox_sync.hpp` 的标记行改掉重新 configure → `slam_toolbox_port/CMakeLists.txt:68` 抛 `FATAL_ERROR ... found 0 and 1`（rc=1），随后按 sha256 逐字节还原 | ✅ |
| P1 `tools\README.md` 漏两个工具 | 文里 `_toolchain` 5 处、`check_cross_platform` 4 处 | ✅ |
| P1 `run_ctest.py` 第五棵树 | 文档字符串的 "five test trees" 与 `TREES` 一致；`--list` 打出 `sim pytest ... ok` 一行；`run_ctest.py sim` 真跑到 69 passed | ✅ |
| P2 `%TEMP%` 残留 | 完整验收（7/7）跑完后 `acceptance-*` 残留 **0**、报告里 `kept` 为空；两处都是 `mkdtemp` + `rmtree` | ✅ |
| P2 根目录 `target` | 根目录条目为 `['.github', '.gitignore', '.pytest_cache', '.scratch', 'README.md', 'bridge', 'gRPC', 'nav2', 'tools']`，没有 `target` | ✅ |
| P2 三个老入口硬编码 | 见第十一节（那一节就是改动与实测） | ✅ |
| P2 `bridge/linux` | Ubuntu 上重跑整条生命周期：`start`（pid 1663490）→ `topic list -t` 打出同样七条带类型 → `topic pub --rate 10 --duration 6` → 日志 `^ /cmd_vel ... msgs=61 failed=0` → `stop` 后 `pid` 返回 1 | ✅ |
| P2 顶层不是 git 仓库 | **2026-09-17 傍晚做掉**（第十七节）：`git init -b main` + `git add -A` = **4498 个条目**，新增 `.gitattributes`（`* text=auto eol=lf`，`.bat` / `.ps1` CRLF）。**仍然没有提交**：首次提交是「要不要把 220 MB 的 `third_party` 收进历史」的决定，留给仓库所有者 | ✅（提交待定） |
| 功能缺口（已知，非新发现） | 现场复核当时成立：`rclcpp_backend.cpp`（279 行）只有话题层（`Publish` 3 处，六个 service / action RPC 全 0）；`type_registry.cpp` 38 条 codec；`rosbag2` / `rclpy` 只出现在文档里。**2026-09-17 晚更正**：service 那半在证据 57 落地，action 那半在证据 58 落地，这一格只剩 codec 表与 `rosbag2` / `rclpy` | ✅ 如实（已过时） |

验收本身也是重跑的：**7/7**（`topic_list` 冷启动 2.0 s、`scan_rate` 9.54 Hz / 55 个采样、
`uplink` 0.81 m、`nav2_demo` 11.72 m / 266 个路径点、`slam` 473x473 @ 0.050 m 换进程读回 440 个
占据格），报告在 `bridge\docs\validation\acceptance-report.json`。

## 十三、`ros2_shim_graph_rewrite` 的跨进程那一半关上了（2026-09-17 深夜）

第七节第 6 条、第八节第 6 条里那个「只剩跨进程那一半」的缺口，这一轮补完了。改动、断言细节与反向
验证记在 `04-gaps.md` 第十六节，这里只留结论和这一轮的现场：

| 条目 | 现场 |
| --- | --- |
| 改法 | `graph.hpp` 的 `flush_locked()` 检查 `install_file()` 的返回值，失败置 `dirty_`、交给重刷线程**重新写 `.tmp` 再换**（原地重试同一个 install 不成立：`ReplaceFileW` 失败会把 `.tmp` 带走）；析构**先停线程再删文件** |
| 新用例 | `ros2_shim_graph_rewrite_cross_process`：真起一个子进程、按 800 ms，父进程连写 5 条记录之后**什么都不再做** —— 放手后自己落地 |
| 反向验证 | 把 `flush_locked()` 改回「丢掉返回值」重编：同一个用例 **rc=8、10.88 s、`(5 missing)`**；按 sha256 还原后 rc=0 |
| 树 | 兼容层 + 魔改 nav2 **32/32、52.27 s**（31 → 32）；`bridge\windows` 因为也 include 这份头，重编后 **4/4、2.35 s** |
| 稳定性 | 新用例单独重跑 **10 轮 10 干净**（0.88~1.02 s，单轮 0.95 s）；`ctest -j 8` 本地 4 轮全绿（19.1 s 那轮起） |
| **Linux** | 四个改动文件推到 `ubuntu-ASUS`：`python3 nav2/ros2_shim/build.py` **410/410 rc=0**、`ctest --test-dir nav2/ros2_shim/build` **32/32、15.09 s**（POSIX 分支编过跑过） |
| `%TEMP%` | 两条图用例都改成**问注册表**要自己的目录（`file.parent_path()`，ctest 会给每条用例加 `NAV2_SHM_NAMESPACE` 后缀）并跑完自清：`ros2_shim_graph_rewrite_test_*` 残留 **3 → 0** |

**这条证据不能证明**：文件被按住的那段时间里其他进程读到的仍然是上一版 —— 这是「文件即通道」的固有
性质；它证明的是丢写不再**永远**停在上一版。`-j 8` 的并行重复性这一轮没有重跑。

## 十四、service 表 11 → 21：slam_toolbox 的九个 srv 补上了（2026-09-17 深夜）

第七节 7.1 里那条「slam_toolbox 的 11 个 srv 只认 2 个」和第八节第 1 条里那句「service 11 / 83」，
这一轮一起合掉了。改动只有 `bridge/windows/src/service_registry.cpp`（加 10 条类型）和
`bridge/windows/tests/service_roundtrip.cpp`（对应的往返用例）；改动细节、字段表和完整现场记在
`04-gaps.md` 第十七节，这里只留结论和这一轮的数字。

| 条目 | 现场 |
| --- | --- |
| 表 | 11 → **21**（9 个 slam_toolbox srv + `nav_msgs/srv/GetMap`），`ros2 interface list` 58 → **68** |
| 用例 | `bridge_service_roundtrip`：**156 checks / 0 failure**，9 个新类型各一次真往返；断言改成 `supported_services().size() == 21` |
| `bridge\windows` | 重编后 ctest **4/4、2.79 s**（`bridge_service_roundtrip` 1.09 s） |
| **Linux**（`ubuntu-ASUS`，8 核） | 两个改动文件推过去，同一套 `build_bridge.py` + ctest：**4/4、2.07 s**，用例同样是 156 checks / 21 条 |
| 活节点 | 九条新类型**全部**在活节点上真调过、全部走共享内存通道、不经守护进程：`slam_bridge_node.exe` 上 7 条 —— `reset`（`result: 0 (success)`）、`clear_changes`、`pause_new_measurements`（`status: true`）、`clear_queue`（`status: true`）、`toggle_interactive_mode`、`manual_loop_closure`、`dynamic_map`（**472x472** 的图、222784 格）；`merge_maps_kinematic.exe` 上 2 条 —— `add_submap`（`filename` 原样到达节点）、`merge_submaps`。`save_map` 也调了：通道通、节点内部失败 |

**这条证据不能证明**：`save_map` 写出了地图文件 —— 它的类型通道通了（`name.data` 到得了节点），
但节点内部走的是上游那句 `system("ros2 run nav2_map_server map_saver_cli ...")`，本机没有 `ros2 run`，
必失败，响应是 `result: 255 (undefined failure)`；存图仍然用 `map_saver_cli.exe`。也不证明
`add_submap` / `merge_submaps` 的语义（前者故意传了不存在的文件、后者零子图空跑）——
验的是通路而不是行为。


## 十五、service 表 21 → 30：nav2 自己的九个 srv 补上了（2026-09-17 下午）

7.1 里 service 那一栏是 21、7.2 里 `ros2 interface list` 是 68，这一轮一起动了。改动仍然只有
`bridge/windows/src/service_registry.cpp` 和 `bridge/windows/tests/service_roundtrip.cpp`；
九条类型的字段表与活节点现场在 `04-gaps.md` 第十八节，这里只留结论和数字。

| 条目 | 现场 |
| --- | --- |
| 表 | 21 → **30**（9 个 `nav2_msgs` srv：四条 costmap 清理 + `GetCostmap` + `GetCosts` + `IsPathValid` + `LoadMap` + `SaveMap`），`ros2 interface list` 68 → **77** |
| 用例 | `bridge_service_roundtrip`：**224 checks / 0 failure**，9 个新类型各一次真往返；断言 `supported_services().size() == 30` |
| `bridge\windows` | ctest **4/4、3.71 s**（`bridge_service_roundtrip` 2.16 s） |
| **Linux**（`ubuntu-ASUS`，8 核） | 两个改动文件在两边的 sha256 一致，`ctest --test-dir bridge/windows/build`：**4/4、2.25 s** |
| 活节点 | 在活着的 nav2 栈上真调了 7 条：四条清理 + `get_costmap` + `get_cost` + `is_path_valid`，其中五条回 `success: true` 并带着真数据（`get_costmap` 是一张 480 x 480 @ 0.05 的 `master` 图层）；`LoadMap` / `SaveMap` 那一轮没起 `map_server`，只在单元测试里往返过 |

**这条证据不能证明**：清理真的把代价地图清空了（只拿到节点回的成功），也不证明 `LoadMap` /
`SaveMap` 在活节点上的行为；这九条服务全是**本机节点自己提供**的，`ros2 service call` 经共享内存
直达节点、**不经过守护进程**，所以它证明的是「本机的 ROS2 表面又补齐了一截」，不是跨机链路。


## 十六、service 表 30 → 38：nav2_msgs 的 srv 认到 20/20（2026-09-17 下午）

第十五节把 nav2 的 costmap / planner / map_server 九条补上、7.1 那一栏从 21 改到 30；这一轮把
`nav2_msgs` 剩下的 8 条补完，那一栏就满了。改动仍然只有
`bridge/windows/src/service_registry.cpp` 与 `bridge/windows/tests/service_roundtrip.cpp`；
字段表与活节点现场在 `04-gaps.md` 第十九节，这里只留结论和数字。

| 条目 | 现场 |
| --- | --- |
| 表 | 30 → **38**（`nav2_msgs` 的 `AddExclusionZone` / `RemoveExclusionZone` / `AddShapes` / `RemoveShapes` / `GetShapes` / `DynamicEdges` / `SetRouteGraph` / `ReloadDockDatabase`），`ros2 interface list` 77 → **85**；**`nav2_msgs` 的 20 个 srv 全部在表里** |
| 用例 | `bridge_service_roundtrip`：**289 checks / 0 failure**，8 个新类型各一次真往返（含 `Point32[]`、两个消息列表、`UUID` 与 `EdgeCost` 列表）；断言 `supported_services().size() == 38` |
| `bridge\windows` | 重编 8/8；ctest **4/4、3.62 s**（`bridge_service_roundtrip` 1.99 s） |
| **Linux**（`ubuntu-ASUS`，8 核） | 两个改动文件推过去、两边 sha256 一致；重编 8/8，`ctest --test-dir bridge/windows/build`：**4/4、2.61 s** |
| 活节点 | 七条真调过：`collision_monitor` 上加 / 删 / 全删禁区（节点回 `Added zone ... to source 'scan'` 这类真话）、`vector_object_server` 上 `get_shapes` → `add_shapes` → `remove_shapes all_objects=true` → `get_shapes`（最后一个回答从两个形状变成空）、`route_server` 上 `set_route_graph`（坏路径 false、真图 true）、`docking_server` 上 `reload_database`（false）。`DynamicEdges` 那一条的插件这一次没实例化，只有单元测试 |

**这条证据不能证明**：`DynamicEdges` 在活节点上的行为（上面最后一句）；禁区真的改变了碰撞行为
（没有再去跑一遍 `/cmd_vel` 看车会不会被那个圆挡住）；`set_route_graph` 的 `success: true` 之后
图真的能算出一条路线。这八条服务全是**本机节点自己提供**的，经共享内存直达、不经过守护进程。

## 十七、第八节排第一的那条关上了：上行默认表，外加 `%TEMP%` 图目录自清（2026-09-17 傍晚）

第十一节的表里只剩三类：功能缺口（`rclcpp_backend` 的 service/action、编解码表偏窄）、
`git` 那一条、以及第八节里排第一的「上行白名单」。这一轮做掉两类。

| 项 | 现场（第一手） | 结论 |
| --- | --- | --- |
| 上行默认表 | `Options::uplink` 从 3 个名字变 5 个（加 `/plan`、`/behavior_tree_log`）；全栈一次运行：`uplink=5`、`/plan msgs=16`、`/behavior_tree_log msgs=15`、`failed=0`；机器人侧 `[sub] /plan #16` 逐字段解出（`frame_id: map`、`position.x`）。`ros2.bat` / `ros2_bridge.bat` 一行没改 | ✅ |
| `%TEMP%` 图目录 | 清存量 56 个（54 个 0 字节）；两个关闭点在 `graph.hpp`（析构 + `collect()` 扫完僵尸之后），判据都是「`remove` 对目录只在空的时候成功」；重编 168.9 s + shim 树 **32/32**，跑完目录数 **3 → 3**（之前同一次运行留 14 个） | ✅ |
| 仓库是 git 仓库 | `git init -b main`、`git add -A` = 4498 个条目、`.gitattributes` 写死 `eol` 约定 | ✅（首次提交留给仓库所有者） |
| `rclcpp_backend` 的 service / action | **关掉了**（2026-09-17 深夜，证据 58）：service 走证据 57，action 走这一轮 —— `rclcpp_backend.cpp` 541 → 1235 行，四个 action RPC 落地；hermetic ctest `actions_rclcpp` 22 条 + 真 gRPC 19 条全过，两个平台都编过 | ✅ 关 |
| 编解码表 | 话题 38 / 216、service 38 / 83 复核仍成立；**action 20 / 20 补满了**（2026-09-17 深夜，证据 61） | ⛔ 仍开着（只剩话题与 service） |

三件事的完整证据分别在 `03-validation.md` 的证据 54（上行）与证据 55（`%TEMP%`）。

**这条不能证明什么**：上行那条的 `failed=0` 只覆盖「agent 收下了」，`/behavior_tree_log` 的字段
没有在机器人侧解出来（那边没有这个 schema，按字节收下）；`%TEMP%` 那条只在 Windows 上重跑过。
另外当时**没有跟着改验收脚本**：`acceptance_check.py` 的 `check_uplink` 仍然只看 `/cmd_vel`。
**2026-09-17 晚补上了**：新检查 `uplink_nav2` 把那两条 nav2 名字变成长度门禁 —— 默认表里没有它、
或者转发出不去，验收就红（证据 56；旧三名字默认表下是 5/6，只有它红）。

## 十八、验收从 8 条变 9 条：它自己藏了一次失败（2026-09-17 深夜）

第十七节把最后一条功能缺口关掉之后跑了一次完整验收：**8/8 通过**，只在最后多了一行
`[warn] /sim/reset exited 1`。那行是一个真缺陷的唯一痕迹 —— **请求消息没有字段的服务，
跨机一条都调不通**（`std_srvs/srv/Trigger` / `Empty` 四条全超时，`SetBool` / `Toggle` 正常）。
根因在仿真对端的 CDR 解码器，完整证据见 `03-validation.md` 证据 59，缺口口径见
`04-gaps.md` 第二十一节。

| 项 | 现场（第一手） | 结论 |
| --- | --- | --- |
| `/sim/reset` 一直失败 | 5 s 超时、退出码 1；守护进程表 `calls=0 failed=3`；stderr 日志 `undecodable std_srvs/srv/Trigger_Request request: 4 trailing byte(s)` | ✅ 已修（`bridge/ubuntu/ros2_bridge_sim/{cdr,messages}.py`） |
| 同一类别的四条服务 | `/sim/reset`、`/sim/status`、`/sim/pause`、`/sim/resume` 全超时；`/sim/set_paused`（有字段）`success: true` | ✅ 修后 `success: true`，`x=0.000 y=0.000` |
| 仿真自己的测试 | `tests/test_services.py` 原来**断言**空请求是 4 字节（`"an empty request is just the CDR header"`）——对端只跟自己比 | ✅ 断言改成手写的 8 字节线上形态，另加两条 |
| 仿真测试树 | 69 → **71 passed**（`tools/run_ctest.py sim`）；改的过程中自己写错一版，被这棵树当场红出 6 条 | ✅ |
| 验收的洞 | `/sim/reset` 是收尾杂事，**退出码被丢掉**，只打 `[warn]` —— 8/8 的「8」里不含它 | ✅ 现在是第 9 条真检查 |
| 完整一轮 | **9/9**，`[PASS] reset (0.0 s) - back at (0.000, 0.000), nothing commanding the wheels` | ✅ |

新检查 `reset` 断言的是**效果**而不是退出码：回应必须 `success: true`，`/odom` 必须回到原点，
且没有命令还在推车轮。原来那种「调用一次、看退出码、不看结果、还不计入总分」的写法，
是这一类缺陷能活下来的直接原因。

**这条不能证明什么**：只重跑了仿真对端，真 ROS2 那一侧（`/probe_trigger`）没有复测；
Windows 侧编码器「只补空体、不补 1–3 字节的体」那条待办（证据 57 第五节）**仍然开着**，
而仿真对端的编码器这一轮已经按完整规则改了，两边在这一点上不一致。
