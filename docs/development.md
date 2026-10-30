---
layout: default
title: 开发指南 Development
---

# 开发指南 Development

> **[中文](#中文) · [English](#english)**

---

# 中文

## 目录结构

顶层：

```
CPU/
├─ README.md                目标、结构、怎么用、怎么开发
├─ GETTING_STARTED.md       新手入口：五分钟说清能干什么、能在哪跑、怎么跑起来
├─ BUILD.md                 编译 / 启动 / 停止速查
├─ docs/                    ★ 本文档站（GitHub Pages 的源）
├─ bridge/                  ★ 胶水层：Windows 守护进程 + ros2 CLI + Ubuntu 仿真 + 文档与验收
├─ gRPC/ros2_gRPC_sdk/      ★ 通信 SDK：proto、客户端/服务端库、示例、自带 ctest
├─ code/Desktop/racecar/    小车的建图（SLAM）+ 导航（nav2）工作区
├─ code/Desktop/rubblish/    从 racecar 里搬出去的非建图 / 导航部分，原样保留、不参与构建
├─ nav2/                    ★ Windows 上的 ROS2 兼容层 + 移植进来的 nav2 / slam_toolbox
├─ tools/                   仓库级开发工具（跑测试、压测、文档体检、证据引用扫描）
├─ .scratch/                历次迭代的实测证据与一次性探针
├─ .gitignore               忽略规则
└─ .gitattributes           换行约定（`* text=auto eol=lf`；`.bat` / `.ps1` 是 CRLF）
```

三个 ★ 子项目的骨架见 [架构](architecture.html)。

## 铁律一：编译只用 6 个大核心

本机是 Intel Core Ultra 5 245K（Arrow Lake，无超线程）：14 个逻辑核 = 6 个 P-core + 8 个
E-core。上面所有构建入口默认 `-j 6`，并且把进程**钉在** `{0,1,10,11,12,13}` 上 —— 子进程
继承亲和性掩码，所以 cmake / ninja / g++ 全留在大核。

```powershell
python tools\_cores.py                 # 本机判定结果： [0, 1, 10, 11, 12, 13] / mask 0x3C03
$env:BIG6_CORES = "0,1,10,11,12,13"    # 覆盖检测（按需）
$env:BIG6_NO_PIN = "1"                  # 只检测不绑定（按需）
```

判定不是照抄文档：`GetLogicalProcessorInformationEx` 的 `EfficiencyClass` 与实测一致
（400M 次依赖自增，大核 0.0811 s / 小核 0.0876 s，8% 的差距正是 5.2 GHz vs 4.6 GHz），
规则是「取成员数较少的那一类」。检测与绑定只有一份实现：`tools/_cores.py`。

## 铁律二：五棵测试树全绿

| 树 | 怎么跑 | 用例数 |
| --- | --- | --- |
| `nav2\ros2_shim` | `python nav2\ros2_shim\build.py` 然后 `python tools\run_ctest.py shim` | 32 |
| `nav2\shm_transport` | `python tools\run_ctest.py shm` | 1 |
| `bridge\windows` | `python bridge\scripts\build_bridge.py --test` | 6 |
| `gRPC\ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py --test` | 4 |
| `bridge\ubuntu\ros2_bridge_sim\tests` | `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q` | 71 |

## 铁律三：不改上游 nav2 源码

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

**当前唯一一处上游改动**：`nav2_mppi_controller/tools/parameters_handler.hpp` 的两行
`value_to_string()`。

## 走一遍：加一个跨机消息类型

以「Ubuntu 发得出来、Windows 上也认这个类型」为目标，一共六步：

1. **生成结构体**：`nav2\ros2_shim\generate.ps1`（或按它里面的参数手动跑
   `tools\generate_messages.py --root ... --output generated\include --python-output python`）。
   扫描顺序有意义：先 overlay 后上游，先命中者胜。生成物进 `generated/`，**不要手改**。
2. **教桥上怎么转码**：`bridge\windows\src\type_registry.cpp` 里有三处要加 —— 编码分支、
   解码分支（两个 `if constexpr` 链），以及底部的表项
   `{"pkg/msg/Type", makeCodec<pkg::msg::Type>("pkg/msg/Type")}`。
3. **造基准字节**：`python bridge\tests\generate_fixtures.py` —— 用 `rosbags`（真 ROS2 的
   CDR 实现）生成 `<名字>.cdr` 与对应的 `.shim`，外加 `manifest.tsv`。
4. **逐字节自检**：`bridge\windows\build\bridge_cdr_check.exe` 会拿 fixture 比对自己的转码结果。
5. **让仿真真的发得出来**：把类型加到 `bridge\ubuntu\ros2_bridge_sim\messages.py`。
6. **两棵树都跑一遍**：`python tools\run_ctest.py bridge` 与
   `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q`。

## 走一遍：加一条 ctest

1. 把测试写进 `nav2\ros2_shim\tests\xxx.cpp`（用 `ros2_shim` 提供的接口，断言风格照抄邻近文件）。
2. 在 `nav2\ros2_shim\CMakeLists.txt` 里 `add_executable` + `target_link_libraries(... ros2_shim)`
   + 一行 `add_test(NAME ros2_shim_xxx COMMAND test_xxx)`。
3. 什么都不用做，`NAV2_SHM_NAMESPACE` 会在文件末尾被自动注入；要额外环境变量就写
   `set_tests_properties(... PROPERTIES ENVIRONMENT "NAV2_SHIM_LOG_LEVEL=info")`。
4. `python nav2\ros2_shim\build.py && python tools\run_ctest.py shim -R xxx`。
5. 改测试断言时先确认它**会**失败：把断言改坏、看它红，再改回来 —— 这叫「变异验证」。

## 改哪里

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

## 跨平台构建入口：`tools/_toolchain.py`

每个构建脚本各写一份 MSYS2 路径、`.bat` / `.ps1`、`.exe` 名字，是「挡着 Linux 的构建入口」。
现在工具链解析收在 `tools/_toolchain.py`（`find_tools()` / `build_env()`），
核数收在 `tools/_cores.py`（`big_cores()` / `pin()`），四棵树的构建脚本都走这两个模块：

| 想改什么 | 怎么改 |
| --- | --- |
| MSYS2 装在哪 | `CPU_MSYS2_ROOT=D:\...\MSYS2`，或命令行 `--msys2` |
| 用哪个 cmake / ninja / ctest | `CMAKE=` / `NINJA=` / `CXX=` |
| 只用哪几个核 | `BIG6_CORES=0,1,10,11,12,13`；`BIG6_NO_PIN=1` 关掉亲和性 |

* Windows 上找不到 MSYS2 会报一条照着做就能改的消息；POSIX 上 `bindir` 是 `None`（没有 MSYS2 的
  `bin` 要塞进 `PATH`），编译器按 `c++` → `g++` → `clang++` 找。
* 只有 `g++`、没有 `c++` 的系统（Alpine）上，脚本会把找到的编译器显式写进
  `-DCMAKE_CXX_COMPILER`；不这么做 CMake 的 try-compile 会
  `posix_spawn /usr/bin/c++: No such file or directory`。
* `nav2/third_party/dl/btcpp_master/.../libbehaviortree_cpp.a` 原来是**没人记录的手工前置条件**，
  现在 `python nav2\ros2_shim\build.py` 自己会先把它 configure + build 出来（`--no-btcpp` 可跳过）。

## 文档纪律

这个仓库的文档是**脚本改出来的**，不是编辑器敲出来的，所以有几个固定的坑：

* `.md` 一律 **LF**；写中文的用 **UTF-8 BOM**（否则 Windows 工具按 GBK 猜，整篇变乱码）；
  `.bat` 反过来，必须是 **CRLF**。
* 改文件走 `bridge\scripts\fsedit.py`（`write` / `replace` 两个子命令），替换前先断言
  目标文本只出现一次 —— PowerShell 下 `apply_patch` 会把换行弄坏。
* 收尾体检：`python tools\check_docs.py --quiet`。它抓 CR、缺 BOM、``` 围栏不配对、
  控制字符。错误返回码为 1；缺 BOM 只算警告。
* 动了 `.scratch/` 之前先跑 `python tools\find_scratch_refs.py --check`：那里的文件是
  `bridge/docs` 的证据，路径断了「证据 N」就成了死链。

## 收尾清单

1. `python tools\check_docs.py --quiet` 干净。
2. `python tools\check_structure.py` 干净；加/删过目录就同步根 `README.md` 的「目录结构」和
   那棵树的 README，这条会替你验。
3. 动过的树各自构建 + 测试通过（五棵树见上）。
4. 动了跨机行为 → `python bridge\scripts\acceptance_check.py` 重跑一遍。
5. 动了 `.scratch/` → `python tools\find_scratch_refs.py --check` 为 0。
6. 把新证据（日志、地图、报告）放进 `.scratch/`，把能复用的工具放进 `tools/`。

---

# English

## Layout

```
CPU/
├─ README.md                goals, structure, how to use it, how to develop it
├─ GETTING_STARTED.md       newcomer entry point
├─ BUILD.md                 build / launch / stop cheatsheet
├─ docs/                    ★ this documentation site (the GitHub Pages source)
├─ bridge/                  ★ glue: Windows daemon + ros2 CLI + Ubuntu simulator + docs
├─ gRPC/ros2_gRPC_sdk/      ★ SDK: proto, client/server libraries, examples, ctest
├─ code/Desktop/racecar/    the car's mapping (SLAM) + navigation (nav2) workspace
├─ code/Desktop/rubblish/    the parts moved out of racecar: kept verbatim, not built
├─ nav2/                    ★ the ROS2 compatibility layer + the ported nav2 / slam_toolbox
├─ tools/                   repo tooling (run tests, stress, doc lint, evidence scan)
├─ .scratch/                measured evidence and one-off probes from past iterations
├─ .gitignore
└─ .gitattributes           line-ending rules (`* text=auto eol=lf`; `.bat` / `.ps1` are CRLF)
```

The three ★ trees are detailed in [Architecture](architecture.html).

## Rule 1 — build with only the 6 big cores

The development machine is an Intel Core Ultra 5 245K (Arrow Lake, no SMT): 14 logical cores =
6 P-cores + 8 E-cores. Every build entry point defaults to `-j 6` and **pins** the processes to
`{0,1,10,11,12,13}` — children inherit the affinity mask, so cmake / ninja / g++ stay on the big
cores.

```powershell
python tools\_cores.py                 # detection result: [0, 1, 10, 11, 12, 13] / mask 0x3C03
$env:BIG6_CORES = "0,1,10,11,12,13"    # override the detection
$env:BIG6_NO_PIN = "1"                  # detect only, do not bind
```

The detection is not copied from documentation: `GetLogicalProcessorInformationEx`'s
`EfficiencyClass` agrees with measurement (400M dependent increments: 0.0811 s on a big core vs
0.0876 s on a small one — the 8% gap is exactly 5.2 GHz vs 4.6 GHz), and the rule is "take the
smaller class". There is exactly one implementation: `tools/_cores.py`.

## Rule 2 — all five test trees green

| Tree | How to run | Cases |
| --- | --- | --- |
| `nav2\ros2_shim` | `python nav2\ros2_shim\build.py` then `python tools\run_ctest.py shim` | 32 |
| `nav2\shm_transport` | `python tools\run_ctest.py shm` | 1 |
| `bridge\windows` | `python bridge\scripts\build_bridge.py --test` | 6 |
| `gRPC\ros2_gRPC_sdk` | `python gRPC\ros2_gRPC_sdk\scripts\build.py --test` | 4 |
| `bridge\ubuntu\ros2_bridge_sim\tests` | `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q` | 71 |

## Rule 3 — do not touch upstream nav2 source

Upstream nav2 1.5.1 lives in `nav2/nav2_extracted/navigation2-1.5.1/` and the shim **compiles it
directly** (`nav2_package_sources()` at `nav2/ros2_shim/CMakeLists.txt:96`: it globs each
package's `src/**.cpp` and `plugins/**.cpp`, drops the files carrying their own `main()`, and every
translation unit gets `-include prelude.hpp`).

Three rules follow:

* **If the compatibility layer can solve it** (a missing interface, a type, a platform
  difference), put it in `prelude.hpp` / `include\ros2_shim\` — that is friendliest to future
  upgrades;
* **If upstream really must change**, the change has to be expressible as a patch in
  `nav2\patches\` that `verify_patch.py` can check, otherwise it silently disappears on the next
  upstream bump;
* **To exclude a whole file**, use `nav2_port_package(<target> <package> <files to exclude>)`
  rather than stubbing the file out.

**The one upstream change today**: two lines of `value_to_string()` in
`nav2_mppi_controller/tools/parameters_handler.hpp`.

## Walkthrough — adding a cross-machine message type

Goal: "Ubuntu can publish it and Windows recognises the type". Six steps:

1. **Generate the struct**: `nav2\ros2_shim\generate.ps1` (or run
   `tools\generate_messages.py --root ... --output generated\include --python-output python` by
   hand with the arguments it lists). The scan order matters: overlay first, upstream second, first
   hit wins. The output goes to `generated/` — **do not edit it by hand**.
2. **Teach the bridge to transcode it**: `bridge\windows\src\type_registry.cpp` needs three
   additions — the encode branch, the decode branch (two `if constexpr` chains), and the table
   entry at the bottom: `{"pkg/msg/Type", makeCodec<pkg::msg::Type>("pkg/msg/Type")}`.
3. **Produce reference bytes**: `python bridge\tests\generate_fixtures.py` — uses `rosbags` (the
   real ROS2 CDR implementation) to write `<name>.cdr` and the matching `.shim`, plus
   `manifest.tsv`.
4. **Byte-compare**: `bridge\windows\build\bridge_cdr_check.exe` compares its own transcoding
   against the fixture.
5. **Make the simulator actually publish it**: add the type to
   `bridge\ubuntu\ros2_bridge_sim\messages.py`.
6. **Run both trees**: `python tools\run_ctest.py bridge` and
   `python -m pytest bridge\ubuntu\ros2_bridge_sim\tests -q`.

## Walkthrough — adding a ctest

1. Write the test into `nav2\ros2_shim\tests\xxx.cpp` (use the interfaces `ros2_shim` provides;
   copy the assertion style of a neighbouring file).
2. In `nav2\ros2_shim\CMakeLists.txt` add `add_executable` +
   `target_link_libraries(... ros2_shim)` + one `add_test(NAME ros2_shim_xxx COMMAND test_xxx)`.
3. Nothing else — `NAV2_SHM_NAMESPACE` is injected at the end of the file. For extra environment
   variables write `set_tests_properties(... PROPERTIES ENVIRONMENT "NAV2_SHIM_LOG_LEVEL=info")`.
4. `python nav2\ros2_shim\build.py && python tools\run_ctest.py shim -R xxx`.
5. When changing an assertion, first confirm it **will** fail: break it, watch it go red, put it
   back — "mutation verification".

## Where to change what

| To change | Go to |
| --- | --- |
| The behaviour of the `ros2` command on Windows | `bridge\windows\tools\ros2_cli.cpp` |
| The daemon: discovery / reconnect / mirroring / graph maintenance | `bridge\windows\src\bridge_daemon.cpp` |
| CDR ↔ shared-memory transcoding and the cross-machine type table | `bridge\windows\src\type_registry.cpp` + `include\ros2_bridge\cdr_archive.hpp` |
| Cross-machine service / action forwarding | `bridge\windows\src\service_registry.cpp` / `action_registry.cpp` |
| The gRPC protocol (add an RPC, change a field) | `gRPC\ros2_gRPC_sdk\proto\ros2_bridge.proto`, then regenerate the stubs with `scripts\build.py` |
| The Ubuntu-side simulator (world, lidar, car) | `bridge\ubuntu\ros2_bridge_sim\world.py` / `sim.py` |
| The Ubuntu-side topic / service / action tables | `bridge\ubuntu\ros2_bridge_sim\messages.py` / `services.py` / `graph.py` |
| The relay (merging the two graphs) | `bridge\ubuntu\ros2_link\relay.py` |
| Compatibility-layer interfaces (rclcpp / rcl / graph registry / parameters / tf) | `nav2\ros2_shim\include\ros2_shim\` + `src\` |
| The message-struct generation rules | `nav2\ros2_shim\tools\generate_messages.py`, then regenerate with `generate.ps1` |
| A nav2 server's main / parameter loading | `nav2\ros2_shim\apps\<package>_node.cpp` |
| nav2's own behaviour | `nav2\nav2_extracted\navigation2-1.5.1\<package>\` |

## The cross-platform build entry point: `tools/_toolchain.py`

Every build script used to carry its own copy of the MSYS2 path, the `.bat` / `.ps1` wrappers and
the `.exe` names — and that copy was "the thing standing between the project and Linux". The
toolchain resolution now lives in `tools/_toolchain.py` (`find_tools()` / `build_env()`) and the
core policy in `tools/_cores.py` (`big_cores()` / `pin()`); all four build trees use them:

| To change | How |
| --- | --- |
| Where MSYS2 is installed | `CPU_MSYS2_ROOT=D:\...\MSYS2`, or the `--msys2` flag |
| Which cmake / ninja / ctest | `CMAKE=` / `NINJA=` / `CXX=` |
| Which cores to use | `BIG6_CORES=0,1,10,11,12,13`; `BIG6_NO_PIN=1` disables pinning |

* On Windows, a missing MSYS2 produces a message you can act on directly; on POSIX `bindir` is
  `None` (there is no MSYS2 `bin` to put on `PATH`) and the compiler is looked up as
  `c++` → `g++` → `clang++`.
* On systems with only `g++` and no `c++` (Alpine), the script writes the compiler it found into
  `-DCMAKE_CXX_COMPILER` explicitly; otherwise CMake's try-compile fails with
  `posix_spawn /usr/bin/c++: No such file or directory`.
* `nav2/third_party/dl/btcpp_master/.../libbehaviortree_cpp.a` used to be an **unrecorded manual
  prerequisite**; now `python nav2\ros2_shim\build.py` configures and builds it first
  (`--no-btcpp` skips it).

## Documentation discipline

The documentation here is **produced by scripts**, not typed into an editor, so there are fixed
traps:

* `.md` is always **LF**; Chinese text uses **UTF-8 BOM** (otherwise Windows tools guess GBK and
  the whole file turns to mojibake); `.bat` is the other way round and must be **CRLF**.
* Edit files through `bridge\scripts\fsedit.py` (`write` / `replace`), and assert that the target
  text occurs exactly once before replacing — `apply_patch` under PowerShell breaks the line
  endings.
* Final lint: `python tools\check_docs.py --quiet`. It catches CR bytes, a missing BOM,
  unbalanced ``` fences and control characters. It returns 1 on errors; a missing BOM is only a
  warning.
* Before touching `.scratch/`, run `python tools\find_scratch_refs.py --check`: those files are
  the evidence behind `bridge/docs`, and a broken path turns "evidence N" into a dead link.

## Finish-up checklist

1. `python tools\check_docs.py --quiet` is clean.
2. `python tools\check_structure.py` is clean; if you added or removed directories, update the
   "directory structure" section of the root `README.md` and that tree's README — the checker
   verifies this for you.
3. Every tree you touched builds and its tests pass (the five trees above).
4. If you touched cross-machine behaviour → re-run `python bridge\scripts\acceptance_check.py`.
5. If you touched `.scratch/` → `python tools\find_scratch_refs.py --check` returns 0.
6. Put new evidence (logs, maps, reports) into `.scratch/`, and reusable tooling into `tools/`.
