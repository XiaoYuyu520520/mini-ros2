# 对 vendor 源码的修改：两份 patch，一份检查

`slam_toolbox` 是唯一一个**原样 vendor 进仓库**的上游包（`nav2/third_party/slam_toolbox`，
2.8.5），所以它的源码里躺着三处本机改动。改动本身都是对的，但"改动直接躺在 vendor 目录里"
这件事本身是升级风险：下次同步上游时，谁也不知道该重放什么。两处是平台行为（Windows 的文本
模式，Linux 上不会出现），一处是析构顺序（**Windows 上完全不报错，Linux 上必炸**）——后者
才是最容易在 re-vendor 时被静默丢掉的那一类。

这个目录就是那份清单。

| 文件 | 作用 |
| --- | --- |
| `slam_toolbox-ucrt-binary-archive.patch` | 改动一：Karto 的存档流按二进制打开（4 处，2 个文件） |
| `slam_toolbox-thread-shutdown.patch` | 改动二：`SynchronousSlamToolbox` 的析构先停线程（1 处，1 个文件） |
| `verify_patch.py` | 三件事：改动还在不在、每处改动是否都有 patch、两份 diff 是否与上游逐字节一致 |
| `README.md` | 为什么改、对应哪个上游版本、升级后怎么重新验证（就是本文件） |

## 改动一：Karto 的存档流按二进制打开

四处，两个文件，每处一行：

```
lib/karto_sdk/src/Mapper.cpp                    Mapper::SaveToFile / Mapper::LoadFromFile
lib/karto_sdk/include/karto_sdk/Karto.h         Dataset::SaveToFile / Dataset::LoadFromFile
```

都是同一件事：把

```cpp
std::ofstream ofs(filename.c_str());          // 文本模式
std::ifstream ifs(filename.c_str());          // 文本模式
```

改成

```cpp
std::ofstream ofs(filename.c_str(), std::ios::binary);
std::ifstream ifs(filename.c_str(), std::ios::binary);
```

每一处旁边留了 `// PORT (ros2_shim):` 注释，`verify_patch.py` 和 CMake 查的就是它。

### 为什么必须改

`serialize_map` 写的 `.posegraph` 是一个 `boost::archive::binary_oarchive`，内容是**任意字节**。
两个 stream 原本都以文本模式打开，而在 Windows 上（UCRT 的 `std::filebuf`，MSVC 的
`fopen` 同理，Linux 上不生效）文本模式有两个后果：

* **读**：`0x1A`（Ctrl-Z）被当作文件结束符。pose-graph 里有 `0x1A` 几乎必然——
  28 MB 的档案里第一个 `0x1A` 通常出现在很前面——于是 `binary_iarchive` 读到一半就撞上
  EOF，抛 `archive_exception: input stream error`。现象是 `deserialize_map` 服务返回
  `false`，节点日志里只有这一行异常，看不出和文本模式有关。
* **写**：`\n` 被翻译成 `\r\n`，档案长度和内容都和写出方不一致。

也就是说 Linux 上（文本模式 = 二进制模式）永远不会出现，一上 Windows 就必现，而且只在
**存档往返**这条路径上出现——纯建图不落盘，所以不看 `.posegraph` 就发现不了。

### 影响面

`slam_toolbox` 的四个服务里两个：`serialize_map`（写）和 `deserialize_map`（读）。
这两个服务是"建一次图、以后加载"的全部，也是 `localization` 模式的前提。

验证它的是 `slam_toolbox_serialization` 这条 ctest（`nav2/slam_toolbox_port/tests/
test_slam_serialization.cpp`）：建一张非空地图 → `serialize_map` → 销毁节点 →
新节点 `deserialize_map` → 比对宽高、occupied/free/unknown 分布和像素。它跑的是真实
的 `slam_toolbox` 节点、真实的 `kartoSlamToolbox`、真实的 Boost 归档，所以少了这四处修改
它必然失败，而不是换个方式通过。

```bat
E:\venvs\py312\Scripts\python.exe nav2\ros2_shim\build.py test_slam_serialization
nav2\ros2_shim\build\test_slam_serialization.exe
```

## 改动二：`SynchronousSlamToolbox` 的析构先 interrupt + join

上游是空体：

```cpp
~SynchronousSlamToolbox() {}          // + 基类那六行真正的收尾
```

`SynchronousSlamToolbox`（派生）在 `on_activate()` 里起一个线程，那个线程每一轮都锁
`q_mutex_`（`src/slam_toolbox_sync.cpp:44`），而 `q_mutex_` 是**派生类**的成员；interrupt 和
join 却写在**基类**析构里（`src/slam_toolbox_common.cpp:243-251`）。C++ 的析构顺序是
派生体 → 派生成员 → 基类体 → 基类成员，于是上游先销毁 `q_mutex_`，之后才去 join 那个还在
不停锁它的线程。

### 为什么必须改

这个窗口在 glibc 上不安静：线程还活着时 `pthread_mutex_destroy` 失败，boost 直接在析构里
断言退出——

```
boost/thread/pthread/mutex.hpp:56: boost::mutex::~mutex(): Assertion
`!posix::pthread_mutex_destroy(&m)' failed.
```

实测在 `.scratch/dtor_order/`（`repro.cpp` + `run_repro.sh`，同一组类、同一个顺序、线程每轮
都走一次锁）：**不加 join 的那条臂 10/10 abort，加了 join 的 0/10**。Ubuntu 24.04、g++ 13。

改法就是让派生析构先 interrupt + join，基类析构随后走过一个空 vector，什么也不做。

### 为什么它比改动一更容易丢

改动一至少在 Windows 上会以 `archive_exception` 现形；改动二在 Windows 上**永远不失败**
（Windows 的 pthread 实现不校验 mutex 的 kind），所以本地怎么跑都是绿的，只有代码跑到
Linux 上才会炸。这正是"必须钉进 patch + configure 门控"的理由：这件事没有任何运行期
用例能替我们守住。

### 影响面

`SynchronousSlamToolbox`，也就是 `sync_slam_toolbox_node` 与 `slam_bridge_node`；触发点是
映射一圈结束时销毁节点（`slam.reset()`）。

## 构建期检查

`nav2/slam_toolbox_port/CMakeLists.txt` 在 configure 时读这三个文件、查这几行；缺任何一项就
直接 `FATAL_ERROR`，不会编出一个"能编译、`deserialize_map` 必挂"（或者"一上 Linux 就 abort"）
的 exe：

```
CMake Error at nav2/slam_toolbox_port/CMakeLists.txt:68 (message):
  third_party/slam_toolbox/include/slam_toolbox/slam_toolbox_sync.hpp has
  lost the ros2_shim port edit to ~SynchronousSlamToolbox: expected one PORT
  (ros2_shim) marker and one thread->join(), found 0 and 1.
```

它检查的是**结果**（那几行在不在），不是**补丁**（patch 文件能不能应用）。这样一次
re-vendor（重新解压上游 zip）会立刻在 configure 阶段停下，而不是等到 `map_server`
加载存档、或者等到同一份代码在 Linux 上跑起来。

## 升级上游之后

1. 把新的上游版本解压进 `nav2/third_party/slam_toolbox`。
2. 按下面的方式重新应用这两份 patch（如果行号漂了，`git apply` 会自己 fuzzy 匹配；匹配不上
   就按两节"改动"里写的手工改，注释照抄），同时把 `verify_patch.py` 里的 `UPSTREAM_TAG`
   和 patch 文件名里的版本换掉。
3. 跑一遍：

   ```bat
   E:\venvs\py312\Scripts\python.exe nav2\patches\verify_patch.py --fetch
   ```

   它做三件事：查这三个修改还在不在（和 CMake 同一个问题）、**每处改动是否都有 patch 认领**
   （有改动没 patch 会报 `COVERAGE`）、以及**重新生成两份 diff** 和 patch 文件逐字节比对。
   `--fetch` 会去 GitHub 拉那个 tag 的原文件（连接被重置会自动重试），所以不需要本地留着
   上游副本；不一致时它把差的那几行打出来。
4. 再跑一遍 `test_slam_serialization`。改动二在 Windows 上测不出来，只能靠第 3 步的门控与
   逐字节比对——这一点上面已经说明过，别指望本地用例。

`--upstream DIR` 是给离线环境用的：指一个上游 `slam_toolbox` 检出（`.scratch\upstream\`
就是按 `--fetch` 的文件布局存的三个文件），跳过下载。

## 重新应用这两份 patch

从 `nav2/third_party/slam_toolbox` 出发，`-p1` 丢掉 `a/`、`b/` 前缀：

```bat
cd nav2\third_party\slam_toolbox
set GIT=D:\WindowsServer2025_1\software\MSYS2\usr\bin\git.exe
%GIT% apply -p1 --check ..\..\patches\slam_toolbox-ucrt-binary-archive.patch
%GIT% apply -p1 --check ..\..\patches\slam_toolbox-thread-shutdown.patch
%GIT% apply -p1         ..\..\patches\slam_toolbox-ucrt-binary-archive.patch
%GIT% apply -p1         ..\..\patches\slam_toolbox-thread-shutdown.patch
```

本机的 MSYS2 里只有 `usr\bin\git.exe`，没有 `patch.exe`（`patch` 包没装）；要 `patch`
就先 `pacman -S patch`。仓库本身不是 git 仓库，但 `git apply` 不要求当前目录在一个 repo
里，所以它照样能用。

这条命令是验过的：把 2.8.5 的 `raw.githubusercontent.com` 原文件放进一个空目录、`git apply -p1`
之后，三个文件和 `third_party/slam_toolbox` 里的**逐字节相同**。

## 还有哪些 vendor 目录被动过

只此一处。同一批移植里对 `nav2/nav2_extracted` 的改动（`nav2_mppi_controller` 的
`parameters_handler.hpp`，两行 `value_to_string()`）在根 `README.md` 里标着，并且已经
在 `bridge/docs/03-validation.md` 有证据；那个目录不算 vendor 不可变，所以没有单独做 patch。
`nav2/slam_toolbox_port/` 是**新增**的移植层，不是 vendor 修改——它整个目录都是本仓库的代码。
