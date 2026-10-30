# third_party · 上游依赖：哪些是源码，哪些只是缓存

这个目录里**既有必须进版本库的源码，也有本机才有的下载缓存**，两种情况要分清：

| 路径 | 是什么 | 进版本库 |
| --- | --- | --- |
| `overlay/` | 消息定义的**第一优先级**根（`generate.ps1` 的扫描顺序里排第一，先命中者胜） | 是 |
| `include/` | 攒出来的头文件包（Eigen、nlohmann/json、nanoflann、tinyxml2），`ros2_shim` 用它当 `NAV2_TP` | 是 |
| `ompl/`、`laser_geometry/` | 只移植了 nav2 用得上的那几个文件（`ompl/README.md` 说明省掉了什么） | 是 |
| `slam_toolbox/` | 上游 slam_toolbox 2.8.5 全树；移植在 `../slam_toolbox_port/`，改动由 `../patches/` 校验 | 是 |
| `common_interfaces/`、`geometry2/`、`rcl_interfaces/`、`robot_localization/`、`geographic_info/`、`navigation_msgs/`、`unique_identifier_msgs/` | 消息包源码，供 `generate_messages.py` 扫描 | 是 |
| `dl/` | **下载缓存**：各上游的 `.zip` / `.tar.gz` 与解包目录（约 107 MB） | **否**（`.gitignore` 忽略） |

`dl/` 是缓存，但**构建真的从里面编译**：`dl/btcpp_master`（BehaviorTree.CPP）、`dl/yamlcpp`、
`dl/eigen` 都被 `../ros2_shim/CMakeLists.txt` 直接引用。`dl/` 里的 `fetch_slam_toolbox.py`、
`scan_slam_deps.py`、`inc_scan.py` 是把它拉回来的脚本 —— 换一台机器要先跑它们（或手工解包），
否则 configure 找不到这些根。

`slam_toolbox/images/` 是上游 README 的截图（54 MB），本仓库的构建与文档都不读它；要瘦身可以从
这里下手，重新解包时会回来。
