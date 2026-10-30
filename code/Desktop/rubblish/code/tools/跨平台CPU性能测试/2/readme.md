脚本已完成：

[下载 CPU 原生单核/多核基准测试脚本](sandbox:/mnt/data/cpu_native_benchmark.py)

### 直接运行

```powershell
python .\cpu_native_benchmark.py 2
```

其中 `2` 采用**从 1 开始编号**，表示：

> 单核整数和单核浮点测试绑定到第 2 个可用逻辑处理器。

指定多核线程数：

```powershell
python .\cpu_native_benchmark.py 2 --threads 16
```

增加测试稳定性：

```powershell
python .\cpu_native_benchmark.py 2 --threads 32 --seconds 1.5 --repeats 5
```

强制重新编译：

```powershell
python .\cpu_native_benchmark.py 2 --rebuild
```

### 脚本功能

* Python 文件内嵌完整 C++17 程序
* 自动寻找 `g++`、`clang++`，也兼容通过 `gcc` 编译C++
* 使用 `-O2 -march=native`
* 自动比较：

  * Python脚本修改时间
  * 原生二进制修改时间
  * C++源码SHA-256
  * 编译器路径和版本
  * 编译参数
  * 操作系统和架构
* 无变化时自动跳过编译
* 单核测试自动绑核
* 多核测试默认占用全部可用逻辑处理器
* 多线程统一起跑，避免线程创建时间干扰
* 每项重复测试并取中位数
* 测试项目：

  * 单核整数
  * 单核浮点
  * 多核整数
  * 多核浮点
* 输出：

  * 测试耗时
  * `Miter/s` 原始吞吐率
  * 单项分数
  * 单核综合分
  * 多核综合分
  * 多核相对单核倍数
  * 总体综合分

Linux 环境已实际完成编译、运行和二次启动跳过编译测试。Windows 分支使用 `SetThreadGroupAffinity` 实现绑核，需要安装 **MinGW-w64 g++** 并把 `g++.exe` 加入 `PATH`。
已在你上传的脚本基础上完成修改。

[下载自动检测与回退版脚本](sandbox:/mnt/data/cpu_native_benchmark_auto_fallback.py)

主要改动：

* 实际创建并执行探针，不再只检查目录是否可写。
* 自动识别 `/sdcard` 的 `noexec/FUSE` 限制。
* 回退顺序：

  1. `CPU_BENCH_CACHE_DIR` 指定目录
  2. 脚本旁的 `.cpu_native_bench_cache`
  3. Android/Linux 的 `/data/local/tmp/cpu_native_bench`
  4. `~/.cache/cpu_native_bench`
  5. `/tmp/cpu_native_bench_<UID>`
* C++ 二进制增加 `--probe` 零负载检测入口。
* 已有二进制即使时间戳和签名正确，也必须通过执行验证才能跳过编译。
* 编译成功但无法执行时，会自动切换到下一个目录并重新编译。
* Linux 编译后二进制自动设置为 `755`。
* 版本更新为 `2.1.0`，会自动触发重新编译。

在你的 Android chroot 中直接执行：

```bash
mv cpu_native_benchmark_auto_fallback.py cpu_native_benchmark.py

python3 cpu_native_benchmark.py 1 --rebuild
```

你的环境预计会显示：

```text
⚠️ 跳过缓存目录 /sdcard/.../.cpu_native_bench_cache：
目录可写但禁止执行文件（可能是 noexec/FUSE）

📁 构建缓存目录：/data/local/tmp/cpu_native_bench
```

若 `/data/local/tmp` 在 chroot 内不可用，它还会继续尝试：

```text
/root/.cache/cpu_native_bench
/tmp/cpu_native_bench_0
```

也可以手动指定：

```bash
CPU_BENCH_CACHE_DIR=/root/.cache/cpu_native_bench \
python3 cpu_native_benchmark.py 1 --rebuild
```

脚本已经在 Linux 环境完成实际的 `-O2 -march=native` 编译、二进制执行探针、正式测试和第二次跳过编译验证。
已在你上传的脚本基础上加入内存带宽测试。

[下载带内存带宽测试的 v2.2 脚本](sandbox:/mnt/data/cpu_native_benchmark_v2.2_memory.py)

新增项目：

* 单线程顺序读取带宽
* 单线程顺序写入带宽
* 单线程内存复制带宽
* 多线程顺序读取带宽
* 多线程顺序写入带宽
* 多线程内存复制带宽
* 内存带宽综合得分
* CPU 总体得分
* CPU＋内存平台综合得分

默认使用：

```text
总工作集：256 MiB
源数组：约128 MiB
目标数组：约128 MiB
```

这样在你测试的7940HX、Jetson Orin Nano和骁龙平台上，通常都能超过末级缓存容量，减少把缓存速度误认为内存速度的情况。

## 运行方式

原来的命令不变：

```bash
python3 cpu_native_benchmark_v2.2_memory.py 8
```

Windows：

```powershell
python .\cpu_native_benchmark_v2.2_memory.py 5
```

自定义内存工作集：

```bash
python3 cpu_native_benchmark_v2.2_memory.py 8 --memory-mib 512
```

快速测试：

```bash
python3 cpu_native_benchmark_v2.2_memory.py 8 \
  --seconds 0.5 \
  --repeats 1 \
  --memory-mib 256
```

正式对比建议：

```bash
python3 cpu_native_benchmark_v2.2_memory.py 8 \
  --seconds 1.0 \
  --repeats 3 \
  --memory-mib 512
```

## 多核内存测试的处理方式

多核整数和浮点仍保持原来的“每线程等量工作”。

但多核内存测试改用了**固定时间吞吐模式**：

* 所有线程同时开始；
* 每个线程持续处理自己的内存区域；
* 快核心可以完成更多轮；
* 慢核心完成较少轮；
* 最终累加所有线程实际传输的字节数。

这样在骁龙的X4＋A720＋A520异构架构上，不会再因为A520最后完成而让X4和A720长时间空等，更接近整个SoC真实可提供的持续内存吞吐。

## 带宽计算规则

读取：

```text
读取字节数 ÷ 时间
```

写入：

```text
写入字节数 ÷ 时间
```

复制：

```text
读取字节数 + 写入字节数
```

因此复制带宽按双向流量计算。例如复制10GB数据：

```text
10GB读取 + 10GB写入 = 20GB总流量
```

输出单位使用十进制：

```text
1 GB/s = 1,000,000,000 Bytes/s
```

需要注意，写入结果没有额外计入缓存写分配、Read For Ownership等隐藏总线流量，所以它是程序可见带宽，不一定等于内存控制器实际流量。
已基于你上传的脚本完成优化。

[下载优化后的 cpu_native_benchmark.py](sandbox:/mnt/data/cpu_native_benchmark_optimized.py)

### 主要优化

**多核 CPU 测试**

* 改为固定时长吞吐模式。
* 每个线程持续计算到截止时间。
* X4、A720、A520 等异构核心可完成不同数量的迭代。
* 不再因为最慢的A520完成等量任务而让X4提前空闲。
* 多次结果按吞吐率取中位数，而不是按耗时取中位数。

**内存带宽测试**

* 默认 `--memory-mib 0`，自动选择工作集：

  * 32线程机器通常约2GiB；
  * 6～8线程设备通常约512MiB；
  * 根据当前可用内存自动缩减，降低OOM风险。
* 每线程每个数组目标约32MiB，减少L2/L3缓存污染。
* 读取改为8路独立整数累加，降低浮点归约对带宽的限制。
* 写入使用连续填充。
* 复制使用连续 `memcpy`。
* 写入和复制后增加内存顺序栅栏。
* 内存测试按实际GB/s取中位数。
* 输出每线程内存分片大小；分片低于16MiB时会给出缓存污染警告。

### 运行

Android/骁龙：

```bash
python3 cpu_native_benchmark_optimized.py 8 --rebuild
```

Windows：

```powershell
python .\cpu_native_benchmark_optimized.py 5 --threads 32 --rebuild
```

手动指定2GiB内存工作集：

```powershell
python .\cpu_native_benchmark_optimized.py 5 --threads 32 --memory-mib 2048
```

快速测试：

```bash
python3 cpu_native_benchmark_optimized.py 8 \
  --seconds 0.5 \
  --repeats 1 \
  --memory-mib 512
```

正式测试建议：

```bash
python3 cpu_native_benchmark_optimized.py 8 \
  --seconds 1.0 \
  --repeats 3
```

由于多核CPU算法和内存算法已经变化，**v2.3成绩不要与v2.2的多核及内存分数直接比较**；三台设备都应使用新脚本重新测试。
你这次的 **957.851 GB/s** 明显是缓存带宽，不是真实内存带宽。原因是旧优化版检测到 Windows 只剩约470 MiB可用内存，于是工作集缩到128 MiB：

```text
每个数组：64 MiB
32线程：每线程每数组仅2 MiB
```

2 MiB分片会大量驻留在L2/L3缓存，导致结果接近1 TB/s。

我已在原脚本基础上完成修正。

[下载修正后的 cpu_native_benchmark.py](sandbox:/mnt/data/cpu_native_benchmark.py)

## v2.4主要修改

### 1. 多线程内存分片轮换

旧版每个线程永远访问自己的固定分片：

```text
线程0一直访问分片0
线程1一直访问分片1
……
```

新版每轮都会轮换：

```text
第1轮：线程0访问分片0
第2轮：线程0访问分片1
第3轮：线程0访问分片2
……
```

即使每线程分片只有2～4 MiB，也不会长期驻留在某个核心的私有缓存。

### 2. 两个数组交替访问

读取测试不再只重复读取 `source`：

```text
第1轮读取 source
第2轮读取 destination
第3轮读取 source
……
```

写入也在两个数组之间交替，复制则交替方向：

```text
source → destination
destination → source
```

因此实际活跃工作集是两个数组之和，而不是单个数组。

### 3. 每轮加入同步屏障

所有线程完成当前分片后，统一进入下一轮，避免：

* 快线程提前多跑几十轮；
* 多个线程同时写同一分片；
* 复制方向切换时发生数据竞争。

### 4. 自动内存分配策略调整

旧版最多只使用可用内存的18%，你的470 MiB可用内存因此只选到128 MiB。

新版：

* 最多使用约60%的可用内存；
* 同时保留至少128 MiB安全空间；
* 你的Windows机器预计会自动选择约256 MiB；
* 可用内存充足时，32线程机器仍会选择约2 GiB。

### 5. 多核CPU测试进一步收紧

整数和浮点分块缩小：

```text
整数：262144 → 65536次/块
浮点：65536 → 16384次/块
```

这可以减少固定时间结束时的尾部超时，尤其适合：

* X4 + A720 + A520
* 性能核 + 能效核
* 各核心速度差距较大的异构处理器

## Windows重新测试

替换脚本后运行：

```powershell
python .\cpu_native_benchmark.py 5 --threads 32
```

版本变为 `2.4.0`，构建签名发生变化，会自动重新编译，不需要手动清缓存。

建议测试前先关闭占用大量内存的软件。你当前只剩约470 MiB可用内存，对内存带宽测试来说偏少。

可用内存足够时，可手动指定：

```powershell
python .\cpu_native_benchmark.py 5 --threads 32 --memory-mib 1024
```

或更严格地使用2 GiB：

```powershell
python .\cpu_native_benchmark.py 5 --threads 32 --memory-mib 2048
```

如果可用内存仍只有470 MiB，不要强行指定1～2 GiB，可能触发页面交换，测出来反而是磁盘和虚拟内存性能。

修正后，7940HX的多线程读取结果应该从不合理的：

```text
957.851 GB/s
```

大幅下降到更符合DDR5平台实际情况的范围。最终数值仍会受内存通道数量、内存频率、时序、功耗模式和后台占用影响。
你这个脚本当前的优化等级是在 `build_flags()` 里设置的：

```python
def build_flags(compiler: str, use_native: bool) -> list[str]:
    name = Path(compiler).name.lower()
    flags = ["-O2", "-std=c++17", "-DNDEBUG"]
```

其中：

```text
-O2
```

就是 GCC/Clang 的编译优化等级。

## 直接修改优化等级

例如改成 `-O3`：

```python
flags = ["-O3", "-std=c++17", "-DNDEBUG"]
```

保存后重新运行：

```powershell
python .\cpu_native_benchmark.py 5 --threads 32 --rebuild
```

Linux/Android：

```bash
python3 cpu_native_benchmark.py 8 --rebuild
```

实际上脚本的构建签名会检测编译参数发生变化，通常即使不写 `--rebuild` 也会自动重编译，但加上最稳妥。

## 各优化等级含义

| 参数       | 含义          | 适用情况       |
| -------- | ----------- | ---------- |
| `-O0`    | 不优化         | 调试代码，性能最低  |
| `-O1`    | 基础优化        | 编译较快，性能一般  |
| `-O2`    | 较完整优化       | 稳定、通用，当前默认 |
| `-O3`    | 激进优化        | 更积极展开、向量化  |
| `-Ofast` | 比 `-O3` 更激进 | 可能牺牲浮点精确性  |
| `-Og`    | 调试友好优化      | 兼顾调试与少量优化  |
| `-Os`    | 优化程序体积      | 嵌入式、小文件    |
| `-Oz`    | 极致缩小体积      | Clang较常见   |

CPU基准测试建议主要比较：

```text
-O2
-O3
-Ofast
```

但要注意：

> 使用不同优化等级的成绩不能直接当作纯CPU性能差异，因为编译器生成的机器指令可能完全不同。

## 推荐增加命令行参数

可以让脚本支持：

```powershell
python .\cpu_native_benchmark.py 5 --opt-level O3
```

### 1. 在 `parse_args()` 中增加

```python
parser.add_argument(
    "--opt-level",
    choices=["O0", "O1", "O2", "O3", "Ofast", "Os"],
    default="O2",
    help="C++编译优化等级",
)
```

### 2. 修改 `build_flags()`

原来是：

```python
def build_flags(compiler: str, use_native: bool) -> list[str]:
    name = Path(compiler).name.lower()
    flags = ["-O2", "-std=c++17", "-DNDEBUG"]
```

改成：

```python
def build_flags(
    compiler: str,
    use_native: bool,
    opt_level: str,
) -> list[str]:
    name = Path(compiler).name.lower()

    flags = [
        f"-{opt_level}",
        "-std=c++17",
        "-DNDEBUG",
    ]

    if use_native:
        flags.append("-march=native")

    if os.name != "nt":
        flags.append("-pthread")

    if name.startswith("gcc") and not name.startswith("g++"):
        flags.extend(["-x", "c++"])

    return flags
```

### 3. 修改调用位置

搜索：

```python
flags = build_flags(compiler, use_native)
```

改成：

```python
flags = build_flags(
    compiler,
    use_native,
    args.opt_level,
)
```

然后就能这样测试：

```powershell
python .\cpu_native_benchmark.py 5 --threads 32 --opt-level O2
```

```powershell
python .\cpu_native_benchmark.py 5 --threads 32 --opt-level O3
```

```powershell
python .\cpu_native_benchmark.py 5 --threads 32 --opt-level Ofast
```

脚本构建签名中本来就保存了 `flags`，所以切换优化等级时会自动判断：

```text
C++源码、编译器、编译参数或平台发生变化
```

并自动重新编译。

## 对你这套测试的预期影响

### `-O2` 到 `-O3`

整数测试可能变化不大，因为整数内核有：

* 64位除法；
* 取模；
* 连续数据依赖。

这些操作难以大规模向量化。

浮点测试可能略有变化，但同样存在较强的数据依赖链，因此未必明显提升。

内存测试则取决于编译器是否：

* 自动向量化；
* 展开循环；
* 将循环识别为 `memcpy` 或 `memset`；
* 使用更宽的SIMD指令。

### `-Ofast`

`-Ofast`通常等价于激进优化并允许不严格遵守IEEE浮点语义，可能启用类似：

```text
-ffast-math
```

这可能改变：

* NaN处理；
* 无穷大处理；
* 浮点运算顺序；
* 舍入行为；
* 浮点依赖关系；
* `sqrt`与除法的优化方式。

因此用 `-Ofast` 后，浮点成绩可能明显提高，但它与 `-O2` 已经不是同一种严格计算语义。

## 推荐配置

日常公平测试：

```text
-O2 -march=native
```

测试编译器最大常规性能：

```text
-O3 -march=native
```

测试允许牺牲浮点严格性的最大性能：

```text
-Ofast -march=native
```

不要把下面两组成绩直接混在一个排行榜：

```text
-O2成绩
-Ofast成绩
```

最好在结果标题里明确打印优化等级。
