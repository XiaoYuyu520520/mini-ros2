---
layout: default
title: 7 进阶开发 · 开发指南
---

# 7.1 开发指南

<div class="overview">
<div class="label">本页总览</div>
<p>本节介绍目录结构、三条硬性规矩，以及怎么加一个跨机消息类型、怎么加一条测试。</p>
<ul>
  <li><a href="#layout">目录结构</a></li>
  <li><a href="#rules">三条铁律</a></li>
  <li><a href="#message">走一遍：加一个跨机消息类型</a></li>
  <li><a href="#ctest">走一遍：加一条 ctest</a></li>
  <li><a href="#where">改哪里</a></li>
  <li><a href="#checklist">收尾清单</a></li>
</ul>
</div>

## 7.1.1 目录结构

```
CPU/
├─ README.md                目标、结构、怎么用、怎么开发
├─ GETTING_STARTED.md       新手入口
├─ BUILD.md                 编译 / 启动 / 停止速查
├─ docs/                    ★ 本文档站（GitHub Pages 的源）
├─ bridge/                  ★ 胶水层：Windows 守护进程 + ros2 CLI + Ubuntu 仿真 + 文档与验收
├─ gRPC/ros2_gRPC_sdk/      ★ 通信 SDK：proto、客户端/服务端库、示例、自带 ctest
├─ code/Desktop/racecar/    小车的建图（SLAM）+ 导航（nav2）工作区
├─ code/Desktop/rubblish/    非建图 / 导航部分，原样保留、不参与构建
├─ nav2/                    ★ Windows 上的 ROS2 兼容层 + 移植进来的 nav2 / slam_toolbox
├─ tools/                   仓库级开发工具
├─ .scratch/                历次迭代的实测证据与一次性探针
└─ package.py               打发布包
```

三个 ★ 子项目的骨架见 <a href="../Architecture/overview">4.1 总体架构</a>。

## 7.1.2 三条铁律

### 铁律一：编译只用 6 个大核心

本机是 Intel Core Ultra 5 245K（Arrow Lake，无超线程）：14 个逻辑核 = 6 个 P-core + 8 个
E-core。所有构建入口默认 `-j 6`，并且把进程**钉在** `{0,1,10,11,12,13}` 上 —— 子进程
继承亲和性掩码，所以 cmake / ninja / g++ 全留在大核。

```powershell
python tools\_cores.py                 # 本机判定结果： [0, 1, 10, 11, 12, 13] / mask 0x3C03
$env:BIG6_CORES = "0,1,10,11,12,13"    # 覆盖检测（按需）
$env:BIG6_NO_PIN = "1"                  # 只检测不绑定（按需）
```

判定不是照抄文档：`GetLogicalProcessorInformationEx` 的 `EfficiencyClass` 与实测一致
（400M 次依赖自增，大核 0.0811 s / 小核 0.0876 s，8% 的差距正是 5.2 GHz vs 4.6 GHz），
规则是「取成员数较少的那一类」。检测与绑定只有一份实现：`tools/_cores.py`。

### 铁律二：五棵测试树全绿

| 树 | 怎么跑 | 用例数 |
| --- | --- | --- |
| `nav2\ros2_shim` | `python nav2\ros2_shim\build.py` 然后 `python tools\run_ctest.py shim` | 32 |
| `nav2\shm_transport` | `python tools\run_ctest.py shm` | 1 |
| `bridge\windows` | `python bridge\scripts\build_bridge.py --test` | 6 |
| `gRPC\ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py --test` | 4 |
| `bridge\ubuntu\ros2_bridge_sim\tests` | `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q` | 71 |

### 铁律三：不改上游 nav2 源码

上游 nav2 1.5.1 的源码放在 `nav2/nav2_extracted/navigation2-1.5.1/`，兼容层**直接从这里
编译**（`nav2/ros2_shim/CMakeLists.txt:96` 起的 `nav2_package_sources()`：glob 每个包的
`src/**.cpp` 与 `plugins/**.cpp`，自动丢掉自带 `main()` 的文件，每个编译单元被
`-include prelude.hpp` 强制注入一层垫片）。

由此得到三条规矩：

* 能用兼容层解决的（缺的接口、类型、平台差异）就**别动上游源码**，加在
  `prelude.hpp` / `include\ros2_shim\` 里 —— 对升级最友好；
* 真必须改上游的，改动要能被 `nav2\patches\` 里的 patch + `verify_patch.py` 表达出来并复现，
  否则下次换上游版本会悄悄丢掉；
* 要排除整个文件，用 `nav2_port_package(<target> <包> <排除的文件...>)`，而不是把文件 stub 掉。

<div class="callout note">
<span class="label">当前唯一一处上游改动</span>
<p><code>nav2_mppi_controller/tools/parameters_handler.hpp</code> 的两行
<code>value_to_string()</code>。</p>
</div>

## 7.1.3 走一遍：加一个跨机消息类型

以「Ubuntu 发得出来、Windows 上也认这个类型」为目标，一共六步：

**第 1 步：生成结构体**

```powershell
nav2\ros2_shim\generate.ps1
```

或按它里面的参数手动跑 `tools\generate_messages.py --root ... --output generated\include --python-output python`。
扫描顺序有意义：先 overlay 后上游，先命中者胜。生成物进 `generated/`，**不要手改**。

**第 2 步：教桥上怎么转码**

`bridge\windows\src\type_registry.cpp` 里有三处要加 —— 编码分支、解码分支
（两个 `if constexpr` 链），以及底部的表项：

```cpp
{"pkg/msg/Type", makeCodec<pkg::msg::Type>("pkg/msg/Type")}
```

**第 3 步：造基准字节**

```powershell
python bridge\tests\generate_fixtures.py
```

用 `rosbags`（真 ROS2 的 CDR 实现）生成 `<名字>.cdr` 与对应的 `.shim`，外加 `manifest.tsv`。

**第 4 步：逐字节自检**

```powershell
bridge\windows\build\bridge_cdr_check.exe
```

它会拿 fixture 比对自己的转码结果。

**第 5 步：让仿真真的发得出来**

把类型加到 `bridge\ubuntu\ros2_bridge_sim\messages.py`。

**第 6 步：两棵树都跑一遍**

```powershell
python tools\run_ctest.py bridge
python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q
```

## 7.1.4 走一遍：加一条 ctest

1. 把测试写进 `nav2\ros2_shim\tests\xxx.cpp`（用 `ros2_shim` 提供的接口，断言风格照抄邻近文件）。
2. 在 `nav2\ros2_shim\CMakeLists.txt` 里 `add_executable` + `target_link_libraries(... ros2_shim)`
   + 一行 `add_test(NAME ros2_shim_xxx COMMAND test_xxx)`。
3. 什么都不用做，`NAV2_SHM_NAMESPACE` 会在文件末尾被自动注入；要额外环境变量就写
   `set_tests_properties(... PROPERTIES ENVIRONMENT "NAV2_SHM_LOG_LEVEL=info")`。
4. `python nav2\ros2_shim\build.py && python tools\run_ctest.py shim -R xxx`。
5. 改测试断言时先确认它**会**失败：把断言改坏、看它红，再改回来 —— 这叫「变异验证」。

## 7.1.5 改哪里

| 想改的东西 | 落点 |
| --- | --- |
| Windows 上 `ros2` 命令的行为 | `bridge\windows\tools\ros2_cli.cpp` |
| 守护进程：发现 / 重连 / 镜像 / 图维护 | `bridge\windows\src\bridge_daemon.cpp` |
| CDR ↔ 共享内存转码、跨机类型表 | `bridge\windows\src\type_registry.cpp` + `include\ros2_bridge\cdr_archive.hpp` |
| 跨机 service / action 转发 | `bridge\windows\src\service_registry.cpp` / `action_registry.cpp` |
| gRPC 协议（加 RPC、改字段） | `gRPC\ros2_gRPC_sdk\proto\ros2_bridge.proto`，然后 `scripts\build.py` 重新生成桩 |
| Ubuntu 侧仿真（世界、雷达、小车） | `bridge\ubuntu\ros2_bridge_sim\world.py` / `sim.py` |
| Ubuntu 侧话题 / 服务 / 动作表 | `bridge\ubuntu\ros2_bridge_sim\messages.py` / `services.py` / `graph.py` |
| 中继（合并两张图） | `bridge\ubuntu\ros2_link\relay.py` |
| 兼容层接口（rclcpp / rcl / 图注册表 / 参数 / tf） | `nav2\ros2_shim\include\ros2_shim\` + `src\` |
| 消息结构体的生成规则 | `nav2\ros2_shim\tools\generate_messages.py`，改完用 `generate.ps1` 重生成 |
| 某个 nav2 服务器的 main / 参数装载 | `nav2\ros2_shim\apps\<包>_node.cpp` |
| nav2 本身的行为 | `nav2\nav2_extracted\navigation2-1.5.1\<包>\` |

## 7.1.6 跨平台构建入口

每个构建脚本各写一份 MSYS2 路径，是「挡着 Linux 的构建入口」。现在工具链解析收在
`tools/_toolchain.py`（`find_tools()` / `build_env()`），核数收在 `tools/_cores.py`
（`big_cores()` / `pin()`），四棵树的构建脚本都走这两个模块：

| 想改什么 | 怎么改 |
| --- | --- |
| MSYS2 装在哪 | `CPU_MSYS2_ROOT=D:\...\MSYS2`，或命令行 `--msys2` |
| 用哪个 cmake / ninja / ctest | `CMAKE=` / `NINJA=` / `CXX=` |
| 只用哪几个核 | `BIG6_CORES=0,1,10,11,12,13`；`BIG6_NO_PIN=1` 关掉亲和性 |

* Windows 上找不到 MSYS2 会报一条照着做就能改的消息；POSIX 上 `bindir` 是 `None`
  （没有 MSYS2 的 `bin` 要塞进 `PATH`），编译器按 `c++` → `g++` → `clang++` 找。
* 只有 `g++`、没有 `c++` 的系统（Alpine）上，脚本会把找到的编译器显式写进
  `-DCMAKE_CXX_COMPILER`；不这么做 CMake 的 try-compile 会失败。

## 7.1.7 文档纪律

这个仓库的文档是**脚本改出来的**，不是编辑器敲出来的，所以有几个固定的坑：

* `.md` 一律 **LF**；写中文的用 **UTF-8 BOM**（否则 Windows 工具按 GBK 猜，整篇变乱码）；
  `.bat` 反过来，必须是 **CRLF**。
* 改文件走 `bridge\scripts\fsedit.py`（`write` / `replace` 两个子命令），替换前先断言
  目标文本只出现一次。
* 收尾体检：`python tools\check_docs.py --quiet`。它抓 CR、缺 BOM、``` 围栏不配对、
  控制字符。错误返回码为 1；缺 BOM 只算警告。
* 动了 `.scratch/` 之前先跑 `python tools\find_scratch_refs.py --check`。

## 7.1.8 收尾清单

1. `python tools\check_docs.py --quiet` 干净。
2. `python tools\check_structure.py` 干净；加/删过目录就同步根 `README.md` 的「目录结构」和
   那棵树的 README。
3. 动过的树各自构建 + 测试通过（五棵树见上）。
4. 动了跨机行为 → `python bridge\scripts\acceptance_check.py` 重跑一遍。
5. 动了 `.scratch/` → `python tools\find_scratch_refs.py --check` 为 0。
6. 把新证据（日志、地图、报告）放进 `.scratch/`，把能复用的工具放进 `tools/`。

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="../FAQ/troubleshooting">6.2 故障排查</a></div>
<div class="next"><span class="dir">下一页</span><span class="ttl">—</span></div>
</div>