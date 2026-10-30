# ros2_shim · Windows 上的 rclcpp，以及跑在它上面的 nav2

这一棵树干两件事：

1. **兼容层**：`include/` 里把 rclcpp / rclcpp_action / rclcpp_lifecycle / rcl / rcutils / rmw /
   tf2 系列 / pluginlib / message_filters 这些 ROS2 C++ 接口造出来（每个编译单元被
   `-include ros2_shim/prelude.hpp` 注入一层垫片），话题走 `../shm_transport` 的共享内存通道
   （`ros2shim<topic>`，`NAV2_SHM_NAMESPACE` 可加前缀隔离两套栈），用图注册表代替 DDS 发现。
2. **nav2 的移植**：上游 navigation2 1.5.1 的源码放在 `../nav2_extracted/`，由这里的
   `CMakeLists.txt` **原地 glob 编译**进来，补齐它需要的一切。

整体说明看 `../README.md`（nav2 这个子项目）与仓库根 `README.md`（怎么用 / 怎么开发）。
这一页只讲本目录的布局与入口。

## 布局

| 路径 | 作用 |
| --- | --- |
| `CMakeLists.txt` | 一棵树构建兼容层 + nav2 + slam_toolbox 的 4 条测试，并把 `../shm_transport` 作为子目录带进来（那 1 条也算在这棵树里）；末尾给每条 ctest 注入 `NAV2_SHM_NAMESPACE=ctest_<名字>` |
| `include/` | 兼容层头文件。先看三个：`rclcpp/shm_config.hpp`（通道命名与槽位大小）、`ros2_shim/prelude.hpp`（垫片）、`ros2_shim/graph.hpp`（图注册表） |
| `src/` | 兼容层实现：`params_file.cpp`（YAML 参数）、`btcpp_groot_stub.cpp` |
| `apps/` | 每个 nav2 服务器的 `main`（planner / controller / bt_navigator / amcl / map_server …）+ 三个演示程序（`nav2_bridge_demo` / `nav2_goal` / `nav2_waypoints`） |
| `tests/` | ctest 的源码。`*_probe.cpp` 是现场探针，**不注册**成 ctest |
| `config/` | nav2 参数 yaml（`nav2_shim_overrides.yaml` / safety / docking_route / map_server）与 `bt/` 行为树 XML |
| `tools/generate_messages.py` | 从 `.msg` / `.srv` / `.action` 生成 `generated/include` 与 `python/pymessages` |
| `tools/make_ros_header_aliases.py` | 生成 `include/` 里的别名头（`rclcpp/rclcpp.hpp` 之类） |
| `tools/pymessages_runtime.py` | 被拷贝成 `python/pymessages/_runtime.py` 的运行时 |
| `generated/include/` | **产物**：C++ 消息结构体（320 个文件），不要手改 |
| `generated_nav2/` | **产物**：遮蔽个别上游头文件用的生成头（例如 `nav2_behavior_tree`） |
| `python/pymessages/` | **产物**：同一批消息的 Python 包（409 个文件），不要手改；见 `python/README.md` |
| `python/py_interop.py`、`python/interop_matrix.py` | 手写的互操作检查（`ros2_shim_py_interop` 那条 ctest 用） |
| `build.py` | 构建入口；`build.ps1` / `run_tests.ps1` / `generate.ps1` 只是外壳，工具链、大核规则、解释器由 `../../tools/_toolchain.py`、`../../tools/_cores.py` 与 `PYTHON` 决定 |

## 构建与测试

```bat
python nav2\ros2_shim\build.py                    :: 配置 + 构建全部（6 个大核，钉在 P-core）
python nav2\ros2_shim\build.py --no-configure     :: 只构建，不重新 configure
python nav2\ros2_shim\build.py controller_server  :: 只构一个目标
python tools\run_ctest.py shim                    :: 33 条，约 55 s
powershell -File nav2\ros2_shim\run_tests.ps1     :: 上面那条的外壳
```

那 33 条 = 本目录 `tests/` 的（含 `ros2_shim_action_status`）+ `slam_toolbox_*` 4 条（源码在 `../slam_toolbox_port/tests/`）
+ `shm_transport` 1 条（`../shm_transport` 作为子目录）。

## 仿真时间

`nav.yaml` 把整栈的 `use_sim_time` 都打开了，所以每个节点的时间戳都是仿真器 `/clock` 的秒数，
而一个跑任务脚本的进程默认还在**墙上时钟**上 —— 拿它对 `get_clock().now()` 和消息里的 `stamp`
相减，差的是 13000 s。三条入口，优先级从高到低：

| 入口 | 什么时候用 |
| --- | --- |
| `--ros-args -p use_sim_time:=true` | 脚本自己会保留 `--ros-args` 尾巴时（`go.py` 会） |
| `ROS2_SHIM_SIM_TIME=1`（`0` 是反面） | 脚本会过滤自己的 `argv`、`-p` 的值得丢掉时；`rclpy.init()` 只在这条命令行没回答时才读它 |
| `python python/shim_run.py --sim-time <脚本>` | 直接跑一个脚本；它写的就是上面那个环境变量 |

`racecar_ctl.py run` 默认自己去图注册表问「现在有几个 `/clock` 发布者」，有就加 `--sim-time`；
`--sim-time` / `--wall-time` 可以覆盖。反过来，**仿真器重启之后要重启整栈** —— 绝对仿真时刻
（`Wait` 行为的 `wait_end_` 之类）在旧纪元上，见 `../../bridge/docs/04-gaps.md` 第二十八节第十一条。

## 改这一棵树的规矩

* 能用兼容层解决的（缺的接口、类型、平台差异）就加在 `include/` 或 `prelude.hpp` 里，
  **别动 `../nav2_extracted/` 的上游源码**；
* 非改上游不可的，改动要能被 `../patches/` 的 patch 表达、并且 `../patches/verify_patch.py`
  能验，否则换上游版本时会悄悄丢掉；
* 要排除整个上游文件用 `nav2_port_package(<target> <包> <排除的文件...>)`，不要把文件 stub 掉；
* 改 `.msg` / `.srv` / `.action` 后走 `generate.ps1` 重新生成（扫描顺序：`../third_party/overlay`
  → `nav2_extracted` 的 `nav2_msgs` / `nav2_dwb_controller` → `../third_party`，先命中者胜），
  产物不要手改。
