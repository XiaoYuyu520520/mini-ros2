# 新手从这里开始

五分钟看懂：这个项目**能干什么**、**在什么机器上能跑**、**怎么跑起来**。

> 这是一个**在 Windows 上直接跑 ROS2 + nav2** 的项目。不需要装 ROS2，也不需要 DDS。
> 但它**不是**官方 ROS2 的替代品 —— 能做什么、不能做什么，下面写清楚了。

---

## 1. 它能干什么

一句话：**把 ROS2 和 nav2 搬到 Windows 上跑，让 Linux 上的机器人或仿真通过局域网和它通信。**

具体能力（都有实测）：

| 能力 | 状态 |
| --- | --- |
| 在 Windows 上用 `ros2 topic list` / `echo` / `pub` 这些命令 | ✅ 不用装 ROS2 |
| 在 Windows 上跑 **nav2 全套**（规划、控制、行为树、AMCL、SLAM） | ✅ 上游源码编译，不改逻辑 |
| Windows 和 Linux 之间**双向**传话题 / 服务 / 动作 | ✅ 局域网 gRPC |
| Ubuntu 上「真 ROS2」和「仿真」两个图**合并成一个**给 Windows | ✅ 见 `start/connector.sh` |
| 同一话题两边都发布时**自动分开**（`/scan` 和 `/real/scan`） | ✅ 不丢数据 |
| Windows 编译 + 打包成一键运行的文件 | ✅ CI 自动出 Windows / Linux 两个包 |

### 它**不是**什么

- **不是官方 ROS2**。接口兼容（`rclcpp` 的头文件、消息类型、`ros2` 命令行），但底层是
  **共享内存 + gRPC**，不是 DDS。所以：
  - 不能和真正的 ROS2 节点在**同一台机器上**直接互相发现（需要走本项目的中继）；
  - 没有完整的 QoS、DDS 发现、多播；
  - `launch` 文件语法只有一小部分。
- **不是零拷贝**。一次发布有 4 次内存拷贝（结构体→字节→共享内存→字节→结构体）。
  实测 `/scan`（5.8 KB）延迟 ~9 µs、吞吐 ~88k msg/s，nav2 的 20–50 Hz 完全够用，
  但大点云/图像不划算。
- **不保证和上游 nav2 行为 100% 一致**。缺失的功能逐条记在
  `bridge/docs/04-gaps.md`，请先看那份。

---

## 2. 能跑在哪

| 平台 | 说明 |
| --- | --- |
| **Windows 10/11 x86_64** | 主体。编译好之后**不需要 MSYS2、不需要 ROS2**，DLL 都在包里 |
| **Ubuntu 24.04 x86_64** | 兼容层和桥都能编能跑；CI 也会出 Linux 包 |
| 其它 Linux | 没测过。需要 g++ 13+、CMake 3.20+ |

跑之前需要的东西：

- **Windows 包**：什么都不用装，解压就能用（里面带 88 个 DLL）。
- **从源码编译**：Python 3.12、CMake、Ninja、一个 C++20 编译器。
  Windows 上推荐 MSYS2 UCRT64；Linux 上 `apt install` 见 `BUILD.md`。
- **和机器人通信**：两台机器在同一个局域网，Ubuntu 侧能 SSH。

---

## 3. 最快的跑法

### 3.1 直接用发布包（推荐新手）

去 [Releases](../../releases) 下载：

- `mini-ros2-windows-x86_64-<版本>.zip`
- `mini-ros2-linux-x86_64-<版本>.tar.gz`

解压后（路径**保持原样**，别把 exe 挪走 —— 脚本按相对路径找文件）：

```powershell
# Windows / PowerShell，点源一次
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

```bash
# Linux
source start/加载模拟ROS环境变量.sh
ros2 topic list -t
```

### 3.2 从源码编译

```powershell
# Windows
python nav2/third_party/dl/fetch_deps.py        # 先拉两个第三方源码树
python nav2/ros2_shim/build.py                  # nav2 + 兼容层
python bridge/scripts/build_bridge.py --test    # 桥 + ros2 命令行
```

```bash
# Linux（先装依赖，见 BUILD.md 3.3）
python3 nav2/third_party/dl/fetch_deps.py
python3 nav2/ros2_shim/build.py
python3 bridge/scripts/build_bridge.py --test
```

编完跑测试（每一步都应该是绿的）：

```bash
python tools/run_ctest.py shim     # nav2 兼容层
python tools/run_ctest.py shm      # 共享内存传输
python tools/run_ctest.py bridge   # 桥
python tools/run_ctest.py grpc     # 通信 SDK
python tools/run_ctest.py sim      # 仿真器（pytest）
```

### 3.3 和机器人 / 仿真连起来

两个终端，先 Linux 后 Windows：

```bash
# Ubuntu：起仿真 + agent，再起中继
bash bridge/scripts/ubuntu_sim_ctl.sh start
bash start/connector.sh up -d
```

```powershell
# Windows：连过去
$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'   # 换成你的 Ubuntu 地址
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t                                 # 应该能看到机器人上的话题
```

更细的步骤、每个参数、排错，见 **`BUILD.md`** 和 **`bridge/docs/02-running.md`**。

---

## 4. 目录里都是什么

```
start/                 一键入口：加载环境、起连接器、起 Windows 服务
bridge/                Windows 守护进程 + ros2 命令行 + Ubuntu 仿真 + 文档
gRPC/ros2_gRPC_sdk/    跨机通信 SDK（proto + 客户端/服务端库）
nav2/ros2_shim/        共享内存 ROS2 兼容层 + 移植的 nav2
nav2/shm_transport/    共享内存环形缓冲（两平台一套算法）
nav2/slam_toolbox_port/  slam_toolbox 的 Windows 移植
tools/                 仓库工具：跑测试、文档体检、结构检查
package.py             打发布包
```

每个子目录都有自己的 `README.md`，写的是「这一层为什么长这样」。

---

## 5. 未来还要发展什么

按重要性排：

1. **补消息类型**。`bridge/windows/src/type_registry.cpp` 是一张手写表，
   表里没有的类型过不去（现在缺 `PointCloud2`、`Image` 等大类型）。
2. **减少拷贝**。现在一次发布 4 次拷贝。`WriteArchive` 逐字段 `push_back`
   且不 `reserve`，大消息会反复扩容 —— 这是最值得优化的一处。
3. **`/parameter_events` 跨机不通**（编解码表里没有这个类型）。
4. **action 的 `_action/` 服务在部分 ROS2 发行版上不可用**（实测 Jazzy 那台机器
   原生 `ros2 action send_goal` 也失败，是环境问题不是本项目）。
5. **`launch` 语法**：上游 nav2 的 `launch/` 那套本兼容层不支持，只能用参数文件。
6. **docking / route** 只到「装得起来」，没有真实的 dock 实例与图文件。

完整的缺口清单在 **`bridge/docs/04-gaps.md`**，每条都有代价评估。

---

## 6. 想换源 / 想合作 / 想改进

**这个项目欢迎 PR 和 Issue。** 但请先看一眼这个仓库的几条硬规矩，
它们不是风格偏好，是踩过坑之后定下来的：

### 规矩

- **能不改上游 nav2 源码就不改**。缺的接口、类型、平台差异，加在
  `nav2/ros2_shim/include/ros2_shim/prelude.hpp` 或 `include/ros2_shim/` 里。
- **必须改上游的**，要写成 `nav2/patches/` 里的 patch + `verify_patch.py` 能校验，
  否则下次换上游版本会悄悄丢掉。
- **改完要跑收尾清单**（`README.md` 末尾有）：文档体检、结构体检、相关测试树。
- **文档是脚本产出的**：`.md` 用 **LF + UTF-8 BOM**，`.ps1` 用 **UTF-8 BOM + CRLF**，
  `.bat` 用 **cp936 + CRLF**。改错编码会让 Windows 上的脚本直接跑不起来。

### 想换源（国内加速）

- 第三方源码：`nav2/third_party/dl/fetch_deps.py` 已经内置了 ghfast / ghproxy /
  gitmirror 几个镜像，会逐个试。加新镜像改 `MIRRORS`。
- pip：`pip install -i https://mirrors.tuna.tsinghua.edu.cn/pypi/web/simple <包名>`
- CI：`.github/workflows/` 里的 workflow 用的是 runner 默认源，需要加速就在步骤里加镜像。

### 提交 PR 之前

```bash
python tools/check_docs.py --quiet        # 文档编码 / 围栏
python tools/check_structure.py           # 目录结构和 README 是否对得上
python tools/find_scratch_refs.py --check # 证据引用没断
python nav2/patches/verify_patch.py       # 上游 patch 还在
```

四条都过，再跑相关测试树，然后提 PR。

---

## 7. 一句话总结

> Windows 上想像 Linux 一样用 ROS2 和 nav2？这个项目就是干这个的。
> 但它有自己的取舍（共享内存代替 DDS、有限的消息类型、异步中继），
> 用之前请读一遍 `bridge/docs/04-gaps.md`，知道它**不做什么**。
