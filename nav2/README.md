# nav2 · Windows 上的 ROS2 兼容层，以及跑在它上面的 nav2

这个目录做两件事：

1. **兼容层**（`ros2_shim/`）：在 Windows 上把 rclcpp / rclcpp_action / rcl / tf2 / 参数 /
   生命周期这些接口造出来，话题走共享内存（`ros2shim<topic>` 通道），用图注册表代替 DDS 发现。
2. **魔改适配的 nav2**：把上游 navigation2 1.5.1 的源码**原地**编进来（`nav2_extracted/`），
   用兼容层顶上它需要的一切，外加 slam_toolbox 的移植（`slam_toolbox_port/`）。

这里没有 ROS2、没有 DDS、也没有 colcon。构建入口：

```bat
python nav2\ros2_shim\build.py                     :: 配置 + 构建全部（只用 6 个大核）
python nav2\ros2_shim\build.py --no-configure      :: 只构建，不重新 configure
python nav2\ros2_shim\build.py controller_server   :: 只构建某个目标
```

## 目录

| 路径 | 作用 |
| --- | --- |
| `ros2_shim/` | 兼容层 + nav2 的移植：接口在 `include/ros2_shim/`，实现在 `src/`，每个服务器的 `main` 在 `apps/` |
| `ros2_shim/generated/` | 由 `tools/generate_messages.py` 生成的消息结构体（C++）与 Python 包，**不要手改** |
| `ros2_shim/generated_nav2/` | 用来遮蔽个别上游头文件的生成头（例如 `nav2_behavior_tree`） |
| `ros2_shim/config/` | nav2 参数文件：`nav2_shim_overrides.yaml`、safety / docking_route / map_server，以及行为树 XML |
| `shm_transport/` | 共享内存传输：通道环形缓冲 + 反射注册表；Windows 与 Linux 两套后端共用一份算法，它自己有构建树和 1 条 ctest |
| `slam_toolbox_port/` | 上游 slam_toolbox 的移植（源码在这里，测试注册在 `ros2_shim` 那棵树里） |
| `third_party/` | 上游依赖源码与解包产物（slam_toolbox、common_interfaces、geometry2、ompl、robot_localization …） |
| `nav2_extracted/navigation2-1.5.1/` | navigation2 1.5.1 的原样源码，`ros2_shim/CMakeLists.txt` 直接从这里 glob 编译 |
| `patches/` | 对上游改动的两份最小 patch + `verify_patch.py`（构建期校验这些改动还在、且每处改动都有 patch 认领） |

## 怎么改

上游源码是**原地编译**的：`ros2_shim/CMakeLists.txt:96` 起，`nav2_package_sources()` 会 glob
每个包的 `src/**.cpp` 与 `plugins/**.cpp`，并且自动丢掉带 `main()` 的文件（那些位置交给
`apps/`）；每个编译单元再被 `-include prelude.hpp` 强制注入一层垫片。由此得到三条规矩：

* 能用兼容层解决的（缺的接口、类型、平台差异）就**别动上游源码** —— 加在 `prelude.hpp`
  或 `include/ros2_shim/` 里，这对将来换上游版本最友好；
* 非改上游不可的，改动要能被 `patches/` 里的 patch 表达、并且 `verify_patch.py` 能验，
  否则换版本时会悄悄丢掉；
* 要排除整个文件用 `nav2_port_package(<target> <包> <排除的文件...>)`，不要把文件 stub 掉。

消息类型的改动走 `tools/generate_messages.py`，改完用 `generate.ps1` 重新生成（扫描顺序
是 overlay 先、上游后，先命中者胜）。

这个目录下的三个 `.ps1`（`build` / `run_tests` / `generate`）现在都只是外壳：工具链、大核规则、
解释器分别由 `tools/_toolchain.py`、`tools/_cores.py` 和 `--msys2` / `BIG6_*` / `PYTHON` 决定，
壳里不再有本机路径（见 `bridge/docs/05-audit.md` 第十一节）。

## 测试

| 树 | 命令 | 用例数 |
| --- | --- | --- |
| `ros2_shim` | `python tools\run_ctest.py shim`，或 `powershell -File nav2\ros2_shim\run_tests.ps1`（外壳，转给前者） | 32 |
| `shm_transport` | `python tools\run_ctest.py shm` | 1 |

上面这 32 条里含 slam_toolbox 的 4 条（源码在 `slam_toolbox_port/tests/`）和
`shm_transport` 的那 1 条（`ros2_shim` 把它作为子目录一起构建）。`ros2_shim` 树里的每条
用例都带一个 `NAV2_SHM_NAMESPACE=ctest_<用例名>`（`CMakeLists.txt` 末尾统一注入），所以
桥守护进程开着也不会和镜像进来的机器人抢同一条共享内存通道。

### 跨平台：`shm_transport` 在 Linux 上也能编能测

`shm_transport` 的算法只有一份（`src/shm_transport.cpp` 里的 `publish` / `subscribe` / 环形游标 /
CRC / `seek_to_*`），平台差异全部收在 `ShmChannel::Impl` 的成员和几个小函数里
（`lock_writer` / `unlock_writer` / `wake_readers` / `prepare_wait` / `wait_for_wakeup` / `close`）：

| 要同步的东西 | Windows | Linux / POSIX |
| --- | --- | --- |
| 通道本体 | `CreateFileMapping(Local\nav2shm_<名>_data)` | `shm_open("/nav2shm_<名>_data")` + `ftruncate` + `mmap` |
| 写者锁 | 命名互斥体 + 5 s 有界 `WaitForSingleObject` | 段内 `pthread_mutex_t`（`PTHREAD_PROCESS_SHARED` + `ROBUST`）+ `pthread_mutex_timedlock` |
| 唤醒读者 | 命名手工事件（`SetEvent` / `ResetEvent`） | 段内 `pthread_cond_t` 广播（`sem_post` 只唤醒一个，多个读者不够用） |
| 段的生命周期 | 最后一个句柄关掉时内核销毁 | 段本身的 `flock` 共享锁：活着的每人一把，内核在进程死掉时收回，最后一把锁的人 `shm_unlink` |

POSIX 侧那两个同步对象就住在共享段里（`ShmSync`，紧跟 layout 头），所以 `kHeaderSize` 在两个平台上
不一样：Windows 是「layout 头对齐到 64」，POSIX 还要加上 `ShmSync`。`header_size` 写进头部，
`open` 用同一个值算槽位偏移。

```bash
# 一个入口，两边都能跑
python nav2/shm_transport/build.py                         # configure + build + ctest
python nav2/shm_transport/build.py --build-dir build-dbg    # Debug 树（带符号）
```

* **`EOWNERDEAD` 必须当成「锁拿到了」**。`ROBUST` 互斥体在持有者死掉时把锁交出来，但只有调用
  `pthread_mutex_consistent()` 的人才能再把它放掉。写者的 `wake_readers()` 原来把 `EOWNERDEAD`
  当成「没锁上」直接返回，`wait_mutex` 就被留成「owner 是自己的线程、状态不可恢复」——下一次
  `publish` 等的是它自己，0% CPU，永远。这不是推演：一个被杀掉的写者留下的段被下一轮 `create`
  收编，第一轮就挂在吞吐用例上，之后每一轮都挂在同一处（gdb 里线程和段内 `__owner` 都对得上）。
* **段的生命周期不能靠段内的引用计数**。计数只有活着的进程会减，被 `kill -9` 的进程没人减，
  名字就永远不回收，下一轮 `create` 收编到的是尸体：环里是上一轮的消息，互斥体是死线程留下的
  状态（上面那条）。现在用 `flock`：`create` 先要独占锁，**拿得到 = 没人在用**，那就把段里的环、
  互斥体、条件变量全部重新初始化；**拿不到 = 有活人在用**，原样收编（Windows 的命名对象本来
  就是这个语义，内核负责）。
* **C ABI 加了 `shm_channel_` 前缀**（`shm_channel_create` / `shm_channel_open` / …）：原来的
  `shm_open` 在 POSIX 上是 `<sys/mman.h>` 的符号，同名不只编不过，`.so` 里导出它还会抢占真正的
  `shm_open`。唯一的调用方是 `python/shm_transport.py`，一起改了。
* 实测：Windows 5 条用例全绿（连跑 3 次）、Ubuntu 7 条全绿（连跑 5 次，含 6 核编译负载下）、
  Alpine/musl（chroot，`/dev/shm` 是 ext4 而非 tmpfs）7 条全绿（连跑 5 次）。用例里新增两条正是
  上面那两条规矩的回归：**写者被杀掉的段必须起一个空环**、**写者还活着的段不准被重置**。

完整的「怎么用 / 怎么开发」在仓库根的 `README.md`；实测证据在 `bridge/docs/03-validation.md`。
