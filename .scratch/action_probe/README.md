# action_probe · 动作通路的三个探针（证据 58）

证据 58 落地前，这里先把「Jazzy 的动作到底长什么样」量清楚了，再动
`gRPC/ros2_gRPC_sdk/src/server/rclcpp_backend.cpp`。所以这不是排障残留，
是那一轮的**前提**：三个探针各自回答一个问题，结论都写进了证据 58 的第一节。

| 文件 | 回答什么 |
| --- | --- |
| `probe.cpp` | 动作那五个类型的 typesupport 能不能**只凭类型名**解析出来（能；顺带量出 `action_msgs` 里没有 `SendGoal.srv`） |
| `probe2.cpp` | 生成结构体的 introspection 布局、嵌套句柄能不能喂 rmw（不能）、`service_is_ready()` 不等的后果；并完整跑一次 send_goal → feedback → get_result → cancel_goal |
| `grpc_action_client.py` | 不走 C++ 测试，改从**真 gRPC** 打 agent（`--backend rclcpp`），一路到机器人上那个 `action_tutorials_cpp` 服务器 |

## 怎么重跑

`probe.cpp` / `probe2.cpp` 要有 ROS2 的机器（本轮是 Ubuntu 24.04 + Jazzy），
`grpc_action_client.py` 只要 `grpcio`，但要有一个跑在 50111 的 agent：

```bash
set +u; . /opt/ros/jazzy/setup.bash; set -u
cd $HOME/code/CPU/.scratch/action_probe
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -- -j 6

# 先让对端有个动作服务器（action_tutorials_cpp 的 fibonacci）
bash $HOME/code/CPU/bridge/scripts/ubuntu_ros2_peer.sh start action agent
./build/action_probe      # 类型名解析
./build/action_probe2     # 布局 + 完整一轮

# 真 gRPC：客户端 -> agent -> RclcppBackend -> 机器人上的动作服务器
$HOME/code/CPU/.venv/bin/python $HOME/code/CPU/.scratch/action_probe/grpc_action_client.py
```

三个都自己造数据、自己收尾，不需要 Windows 那侧参与。
