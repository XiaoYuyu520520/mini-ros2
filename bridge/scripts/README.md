# bridge\scripts · 跨机运维脚本

这些脚本在 **Windows 上跑**，大多通过 SSH 操作那台 Ubuntu（密钥与主机在 `ubuntu_ssh.py` 里，
`tools/check_cross_platform.py` 也用同一份）。它们不进 CI（要两台机器），所以改动要手工验。

那五个 `run_*.py` 是例外：**两平台同一份**，Windows 和 Ubuntu 都直接跑它，各自与
`bridge/windows/` 下的同名 `.bat` 等价（`.bat` 是给 cmd 习惯的外壳，头部也写着这句话）：

| `.py` | `.bat` |
| --- | --- |
| `run_nav2_stack.py` | `nav2_stack.bat` |
| `run_slam_demo.py` | `slam_demo.bat` |
| `run_amcl_demo.py` | `amcl_demo.bat` |
| `run_nav2_safety.py` | `nav2_safety.bat` |
| `run_nav2_docking_route.py` | `nav2_docking_route.bat` |

平台差异全在 `_bridge_platform.py` 里出现三次（`.exe` 后缀、哪些节点在跑怎么停（`tasklist` +
`taskkill` 与 `/proc`）、`.bat` 与 `.sh` 的就绪等待），写新脚本时不要再往里加 `if os.name == "nt"`。

| 脚本 | 作用 |
| --- | --- |
| `build_bridge.py` | 构建入口：configure + build（+ `--test` 跑 ctest）；`--clean` 只允许删 `bridge/windows/build` 之内 |
| `sync_to_ubuntu.py` | 把 Ubuntu 侧代码推过去；`--run` 顺带起仿真（在 Ubuntu 的 tmux 会话 `sim` 里） |
| `run_nav2_stack.py` | **跨平台**：整栈 nav2（AMCL + planner + controller + bt_navigator + behavior + smoother + waypoint_follower + lifecycle_manager），`--goal/--map/--safety/--duration/--params/stop` |
| `save_map.py` | **跨平台**：把地图话题存成 `PGM`/`PNG` + YAML，是这边 `ros2 run nav2_map_server map_saver_cli` 的对位物。`-f BASE` 定文件名（`racecar_ctl.py` 里给的名字按 `racecar/map/BASE` 解析）、`--topic` 默认 `/map`（自己建图时读 `/slam_map`）、`--fmt pgm|png`，其余参数原样转给 `map_saver_cli` |
| `run_slam_demo.py` | **跨平台**：slam_toolbox 建图，参数原样转给 `slam_bridge_node` |
| `run_amcl_demo.py` | **跨平台**：nav2_amcl 定位（`--initial-pose` 不给就从 `/odom` 起） |
| `run_nav2_safety.py` | **跨平台**：旁路起 `velocity_smoother` + `collision_monitor` 并推到 ACTIVE |
| `run_nav2_docking_route.py` | **跨平台**：旁路起 `docking_server` + `route_server` 并推到 ACTIVE |
| `_bridge_platform.py` | 上面几个共用的平台层：环境变量、`.exe`、就绪等待、哪些节点在跑（`live_pids`）与怎么停（`kill_nodes`）、起后台节点、`ros2` CLI、lifecycle 准备与激活 |
| `run_nav2_demo.py` | 不经 bat，直接从 Windows 驱动 nav2 演示 |
| `acceptance_check.py` | 四条验收标准一次跑完（9 条检查，含 SLAM；`--skip-slam` 跳过） |
| `ubuntu_ssh.py` | 跨机执行的底座：`ubuntu_ssh.py [--file x.sh] [--timeout N] "<cmd>"` |
| `ubuntu_sim_ctl.sh`、`ubuntu_ros2_peer.sh`、`ubuntu_ros2_setup.sh` | 在 **Ubuntu 上**跑的辅助脚本（本地放着便于 `--file` 推过去） |
| `fsedit.py` | 精确文本替换小工具，改这个仓库的文件默认用它 |

本机路径、`.exe` 拼写这类东西在过去是写死在这里的，现在工具链解析统一走
`tools/_toolchain.py`、核数走 `tools/_cores.py`；新脚本请照做，不要再写死本机路径。
