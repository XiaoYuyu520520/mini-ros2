# python · 消息生成物，外加两个手写的互操作工具

`pymessages/` 不是手写的：它是 `../tools/generate_messages.py --python-output .` 的产物，
和 `../generated/include/` 里的 C++ 结构体出自同一批 `.msg` / `.srv` / `.action`
（`../generate.ps1` 一次生成两边）。

**不要手改 `pymessages/`**。改消息定义后重新生成：

```bat
powershell -File nav2\ros2_shim\generate.ps1                          :: 用 PATH 上的 python
powershell -File nav2\ros2_shim\generate.ps1 -Python E:\venvs\py312\Scripts\python.exe
```

`pymessages/_runtime.py` 是从 `../tools/pymessages_runtime.py` 拷过来的。

另外两个文件是手写的，配合 `ros2_shim_py_interop` 那条 ctest：

| 文件 | 作用 |
| --- | --- |
| `py_interop.py` | Python 侧的消息编解码；`../tests/py_interop.cpp` 起进程调它，两边比字节 |
| `interop_matrix.py` | 生成/校验互操作矩阵：哪些类型两边都逐字节验过（8 条腿；第 6 条探 action 的 `_action/status`，第 7 条验 `ROS2_SHIM_SIM_TIME` 与命令行 `-p` 的优先级，第 8 条拿这栈自己发的字节当夹具验数组字段） |
| `shim_run.py` | 把 shim 的 `rclpy` / 消息别名 / vendored `nav2_simple_commander` 放到脚本前面再 `runpy` 它；`--sim-time` / `--wall-time` 写的是 `ROS2_SHIM_SIM_TIME`（脚本过滤 `argv` 也丢不掉它），`--paths` 打印它用的目录 |
