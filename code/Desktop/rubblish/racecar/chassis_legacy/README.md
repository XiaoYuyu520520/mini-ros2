# chassis_legacy —— 从 racecar 包里挪出来的底盘 / 视觉文件

这些文件原本在 `code/Desktop/racecar/src/racecar/` 下面。挪到这里是为了让 `racecar/` 只剩建图和导航，
搬的时候一个字节都没改。

| 这里 | 原来在 `src/racecar/` 的 | 是什么 |
| --- | --- | --- |
| `src/*.cpp`（7 个） | `src/` | 旧底盘 / 雷达节点；`CMakeLists.txt` 里早就被注释掉了，没在编译 |
| `launch/Run_car.launch.py` | `launch/` | 启动底盘（EKF + carto + 手柄），对应 `../car.sh` |
| `launch/ekf.launch.py` | `launch/` | robot_localization EKF，发 `odom_combined` |
| `config/ekf.yaml`、`ekf_carto.yaml` | `config/` | 上面两个 EKF 的参数 |
| `config/camera/` | `config/` | usb_cam 参数 |
| `scripts/detct copy.py`、`hsv_show.py` | `scripts/` | 摄像头巡线、HSV 调参 |
| `scripts/record.py` | `scripts/` | 录相机话题 |
| `scripts/py_node.py` | `scripts/` | 空文件 |
| `scripts/racecar_teleop.py` | `scripts/` | 键盘遥控，往 `/teleop_cmd_vel` 发 PWM / 舵机角度；唯一订阅它的 `racecar_driver` 也搬到了 `../../src/racecar_driver/` |
| `scripts/racecar_teleop.py` 在 `CMakeLists.txt` 里的那行 `install(PROGRAMS ...)` | `CMakeLists.txt` | 2026-09-19 一起删掉了（它不在 `racecar/` 的安装列表里了，见下） |

`config/laser_box/x4_laser_filter.yaml` **没有**搬过来：它裁的是雷达角度，属于建图。

搬回去：照上表放回原路径。留在 `racecar/` 的 `CMakeLists.txt` 里那几行 `add_executable` 仍是注释状态；
这里搬走的脚本里，只有 `racecar_teleop.py` 原本挂在 `install(PROGRAMS ...)` 上，那一行也已经删掉，
所以现在 `racecar/` 整棵树里没有任何一处引用这个目录（`tools/check_structure.py` 的悬挂引用扫描也是 0）。

为什么搬它：它发的是 `/teleop_cmd_vel`，那是 PWM / 舵机角度约定的手工遥控话题（真车 `racecar_driver`
的 `manual` 通道），跟建图 / 导航无关；而且它模块级就 `import termios, tty`，Windows 上根本起不来。
搬回去时记得把那行 `install(PROGRAMS scripts/racecar_teleop.py DESTINATION lib/${PROJECT_NAME})` 补上，
否则 `ros2 run racecar racecar_teleop.py` 找不到它。
