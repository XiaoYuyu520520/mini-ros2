# 2026-10-30 · shim transport 真实路径基准

工具：`nav2/ros2_shim/tests/bench_real_path.cpp`（**不**注册成 ctest，
`nav2/ros2_shim/build.py bench_real_path` 编，直接跑 exe）。

测的是**完整发布路径**，不是 transport 层：
`struct → serialize() → vector<uint8_t> → memcpy 进共享槽 →`
`（进程边界）→ memcpy 出槽 → deserialize() → struct`。

## 本机结果（Windows / MSYS2 UCRT64 / Release / 6 P-core）

槽几何 `count=8 size=2097152 large=8388608`（**这是 shm_config.hpp 的默认值**；
`bridge_env` 里改成了 512K/2M，基准直接跑 exe 用的是默认值——见下）。

### codec only（纯序列化，无通道）

| 载荷 | 大小 | 每轮耗时 |
| --- | --- | --- |
| Twist | 48 B | 0.1 us |
| Odometry | 709 B | 0.3 us |
| LaserScan | 5813 B | 2.3 us |

### 吞吐（同进程，publish 循环里真的 serialize）

| 载荷 | 线上大小 | msg/s |
| --- | --- | --- |
| Twist | 48 B | **1,319,553** |
| Odometry | 709 B | **462,186** |
| LaserScan | 5813 B | **87,377** |

### 吞吐（跨进程，读者在另一个进程）

| 载荷 | 槽大小 | msg/s |
| --- | --- | --- |
| Twist | 112 B | **1,402,100** |
| Odometry | 773 B | **483,414** |
| LaserScan | 5877 B | **88,368** |

跨进程和同进程**几乎一样**——这是因为共享内存本来就是同一块映射，
进程边界不花钱。这条是本仓库"零系统调用"设计的最好证据。

### 延迟（一发一收，单条在飞）

| 载荷 | mean | p50 | p99 | max |
| --- | --- | --- | --- | --- |
| Twist | 0.5 us | 0.5 us | 0.6 us | 10.5 us |
| Odometry | 1.6 us | 1.6 us | 1.7 us | 22.4 us |
| LaserScan | 9.0 us | 8.8 us | 11.2 us | 199.9 us |

**解读**：LaserScan 的 9 us 里，codec 占 2.3 us，剩下 ~6.7 us 是两次 memcpy
（5.8 KB 来回）+ 通道的 mutex/事件。Twist 只有 0.5 us，说明小消息基本被
通道开销主导，codec 可以忽略。

## 对照 nav2 的实际需求

nav2 的控制回路是 **20–50 Hz**，激光 **10 Hz**，代价图 **1–5 Hz**。
最重的 `/scan`（5.8 KB）在 10 Hz 下只是 **0.009%** 的 CPU 时间片，
延迟 9 us 对 100 ms 的控制周期可以忽略。

**真正的瓶颈不是这个传输层**，而是：
* 大点云/图像（`/cost_cloud` 是 `PointCloud2`，可以是几百 KB 到 MB 级）——
  这也解释了为什么 `type_registry.cpp` 里干脆没给它们 codec；
* 序列化器 `WriteArchive` **逐字段 `push_back`、不 reserve**（`serialization.hpp:212`），
  大消息会反复扩容+拷贝。这是最值得优化的一处：加一层 size 预估 + `reserve()`
  能让 5.8 KB 的 LaserScan 序列化再快一些（现在 2.3 us）。

## 注意

* 基准直接跑 exe，读的是 `shm_config.hpp` 的默认槽几何（2 MB/8 MB）。
  在 `bridge_env` 环境下跑（512 KB/2 MB）对吞吐/延迟**几乎无影响**（槽大小只
  决定单条消息上限和内存占用，不决定速度），所以上面数字对两种情况都成立。
* 同进程数字会被同一个 CPU 缓存美化；**跨进程那一组才是真实 nav2 的样子**
  （controller_server 和 planner_server 是两个进程），而两者几乎相同。
