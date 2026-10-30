# bridge\linux · Linux 侧的四个入口

与 `../windows/` 的 `.bat` 一对一，是同一件事在 Linux 上的写法；也是
`tools/check_cross_platform.py` 远程那一半真正跑的东西（`start` → `pid` → `stop` → `pid` 应当失败，
整条生命周期，而不是「脚本能解析」）。

| 脚本 | 对应 | 作用 |
| --- | --- | --- |
| `bridge_env.sh` | `bridge_env.bat` | 只设环境变量（PATH、共享内存几何、SDK 配置、日志级别），供别的脚本 `source` |
| `ros2.sh` | `ros2.bat` | Linux 上的 `ros2` 命令，按需拉起守护进程 |
| `ros2_bridge.sh` | `ros2_bridge.bat` | 守护进程的 `start` / `run` / `status` / `pid` / `stop` |
| `wait_bridge_ready.sh` | `wait_bridge_ready.bat` | 轮询 ready 文件，等首次扫描结束 |

```bash
source bridge/linux/bridge_env.sh
bash bridge/linux/ros2_bridge.sh start
bash bridge/linux/ros2.sh topic list -t
bash bridge/linux/ros2_bridge.sh stop
```

注意它与 `../windows/` 是 shell 入口里**唯一真的写了两遍**的一层：`src/` 那些源码两平台共用，
只有这几个入口两边各写一次。nav2 / slam 的启动不再各写一遍，见下。

## nav2 / slam / amcl / 安全链的启动入口在 `../scripts/`，两平台共用

`run_nav2_stack.py`（整栈）、`run_slam_demo.py`（建图）、`run_amcl_demo.py`（定位）、
`run_nav2_safety.py`（速度平滑 + 碰撞监控，旁路）、`run_nav2_docking_route.py`（docking + route，
旁路）是同一份脚本，Windows 和这台机器都跑它；平台差异只在 `../scripts/_bridge_platform.py` 里出现
三次：`.exe` 后缀、`taskkill` 与 `/proc` 的杀进程、`.bat` 与 `.sh` 的就绪等待。环境变量也是它按
`bridge_env.sh` 的同名同值建出来的，所以不必先 `source` 什么：

```bash
export ROS2_BRIDGE_AGENT=127.0.0.1:50051     # 仿真在本机，守护进程连本机的 agent
python3 bridge/scripts/run_nav2_stack.py --goal auto --report /tmp/goal.json
python3 bridge/scripts/run_slam_demo.py --duration 60
python3 bridge/scripts/run_amcl_demo.py --duration 60
python3 bridge/scripts/run_nav2_safety.py --duration 60       # 与栈同时跑：只观察
python3 bridge/scripts/run_nav2_docking_route.py --duration 60
python3 bridge/scripts/run_nav2_stack.py stop
```

栈里的七个进程按 `/proc/<pid>/cmdline` 认（只认本仓库 `nav2/ros2_shim/build/` 里的二进制），
日志一节点一份在 `/tmp/nav2_stack/`。
