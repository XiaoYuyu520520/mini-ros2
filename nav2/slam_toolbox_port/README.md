# slam_toolbox_port · 上游 slam_toolbox 的 Windows 移植

上游全树在 `../third_party/slam_toolbox`（2.8.5），需要 Windows 适配的部分在这里重写或补齐，
编译进 `../ros2_shim` 那棵树 —— **本目录没有自己的构建树**，所以这里没有 `build/` 是正常的。
对上游源码本身的改动由 `../patches/` 里的 patch 表达，`../patches/verify_patch.py` 在构建期校验。

## 布局

| 路径 | 作用 |
| --- | --- |
| `apps/slam_bridge_node.cpp` | 接上兼容层的节点：`/scan` 进、`/map` 出（`bridge\windows\slam_demo.bat` 用它） |
| `include/solver_plugins/eigen_pose_graph_solver.hpp`、`src/eigen_pose_graph_solver.cpp` | 用 Eigen 实现的位姿图求解器插件（上游那套依赖 ROS2 插件加载，这里换成兼容层能加载的形式） |
| `include/karto_compat/uint.hpp` | Karto 的 `uint` 在现代编译器下的兼容头 |
| `tests/` | 4 条 ctest 的源码，注册在 `../ros2_shim` 那棵树里 |

那 4 条是 `slam_toolbox_eigen_pose_graph_solver`、`slam_toolbox_mapping`、`slam_toolbox_laser_points`、
`slam_toolbox_serialization`。

## 跑

```bat
python tools\run_ctest.py shim -R slam_toolbox        :: 只跑这 4 条
bridge\windows\slam_demo.bat --duration 60            :: 端到端：仿真 /scan 进、地图出
python bridge\scripts\acceptance_check.py --skip-slam :: 反过来，跳过 SLAM 那条验收
```

上游全树的 `images/` 是它自己 README 的截图（54 MB），这里不读，见 `../third_party/README.md`。
