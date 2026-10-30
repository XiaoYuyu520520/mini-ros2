# tools · 仓库级开发工具

六个工具加两个共用模块，都不需要额外依赖，直接跑（只有 `check_cross_platform.py` 要 SSH
到那台 Ubuntu 机器）。**它们是「可复用」的那一类**；一次性的探针、日志、实测证据一律进
`.scratch/`（那里的规矩见 `.scratch/README.md`）。

| 文件 | 干什么 | 例子 |
| --- | --- | --- |
| `run_ctest.py` | 给任意一棵测试树跑测试（4 棵 ctest + 1 棵 pytest），并把进程钉在 6 个大核上 | `python tools\run_ctest.py sim -k watchdog` |
| `ctest_stress.py` | `ctest -j N` 连跑多轮，记录哪条测试只在并行下崩 | `python tools\ctest_stress.py --rounds 50 --jobs 8` |
| `check_docs.py` | 文档体检：LF-only、BOM、``` 围栏成对、控制字符 | `python tools\check_docs.py --quiet` |
| `find_scratch_refs.py` | 列出所有被文档/源码引用的 `.scratch\...` 路径 | `python tools\find_scratch_refs.py --check` |
| `check_structure.py` | 结构体检：一棵树一张 README、结构图里的目录真的在、文档点名的入口脚本真的在 | `python tools\check_structure.py --list` |
| `check_cross_platform.py` | 一条命令在 Windows 和 Ubuntu 上分别编译 + 测试（12 项检查），跨平台构建入口的守门人 | `python tools\check_cross_platform.py --local` |
| `_cores.py` | 上面几个共用的「哪 6 个核是大核」检测与绑定 | `python tools\_cores.py` |
| `_toolchain.py` | 上面几个共用的「这台机器上的 cmake / ninja / g++ / ctest 在哪」 | `python tools\_toolchain.py` |

## 说明

* **`.github/workflows/cross-platform.yml` 是唯一的 CI**，两个 job（windows / linux）跑同一批
  入口：三个构建入口 + 四棵 ctest 树 + 上面那棵 pytest 树 + 四个与本机无关的检查
  （`check_docs.py`、`check_structure.py`、`find_scratch_refs.py --check`、
  `nav2/patches/verify_patch.py`）。
  注意它是**该跑的清单**而不是**跑过的记录**：仓库 0 commit、没有远端，所以到 2026-09-17
  为止它一次都没执行过 —— 那些命令都在**本机**逐条跑过，见 `bridge/docs/03-validation.md`
  证据 60 与 `bridge/docs/04-gaps.md` 第二十二节。`check_cross_platform.py` 故意不在 CI 里
  （`--remote` 要 SSH 到那台 Ubuntu，`--local` 与 workflow 那些入口重复）。
* `check_structure.py` 是**目录层的守门人**，三条契约写死在文件顶部的常量里：
  `REQUIRED_READMES`（有自己规矩的目录必须有一份 README）、`REQUIRED_FILES`（文档里点名的
  入口脚本必须真的存在），以及根 `README.md` 的「目录结构」那几张图（图里画到的目录必须
  真的在磁盘上，`build/` / `bin/` / `obj/` 除外 —— 全新 checkout 本来就没有）。
  `--list` 把三份清单原样打出来；以后加目录，改的是那个文件里的常量，不是这里的散文。
* `run_ctest.py` 里有五棵树：四棵 ctest 树 `shim`（32 条）、`shm`（1 条）、`bridge`（6 条）、
  `grpc`（4 条），加一棵 pytest 树 `sim`（`bridge\ubuntu\ros2_bridge_sim\tests`，71 条）。
  树名之后的所有参数原样转发给 ctest / pytest（`--jobs` 只对 ctest 树有用）；也可以直接给
  构建目录路径。`--list` 会把树名、runner 和路径一起列出来。
  pytest 那棵树**在 Windows 上跑**：Ubuntu 的 venv 里只有 grpc / numpy / protobuf，没有 pytest。
  runner 是用 `sys.executable` 起的，所以本机得用 venv 那个解释器
  （`E:\venvs\py312\Scripts\python.exe tools\run_ctest.py sim`）；CI 里是先 `pip install pytest`
  再跑，默认的 `python` 会直接 `No module named pytest`（`bridge/docs/04-gaps.md` 第二十三节）。
  三棵 ctest 树（`shim` / `shm` / `bridge`，共 39 条）都带 `TIMEOUT 300`：这些测试等的共享内存
  通道在写者被强杀之后可以让下一个进程永远等下去（同节第二节），有上界才看得见失败。
* `check_cross_platform.py` 是唯一会动另一台机器的工具：它按 `bridge/scripts/ubuntu_ssh.py`
  的密钥与主机推**构建所需的切片**（不推构建树），两边各跑一遍。`--local` / `--remote` 只跑
  一边，`--quick` 只跑 shim + shm，`--push-only` 只同步。它存在的理由是两个检查真的抓到过
  移植性缺陷：同一条构建入口必须在两边都能跑（写死的 MSYS2 路径、`".exe"` 拼法会在另一台机器
  上当场失败），以及 BehaviorTree.CPP 的静态库必须在远端**从源码重建**（这样"以前手工编过、
  目录还在"就不再是必要条件）。
* `ctest_stress.py` 默认输出到 `.scratch/`：`ctest_stress[<tag>].json`，以及坏掉的轮次对应的
  `stress_fail_NNN.log`。这两个文件名是 `bridge/docs` 引用过的，改名要同步改文档；
  注意默认那次运行会**覆盖**被引用的旧结果，只想自测就用 `--out <临时目录>`（或 `--tag`）。
* `_cores.py` 单独跑会打印本机的 P-core 列表和绑定结果 —— 想确认 `BIG6_CORES` /
  `BIG6_NO_PIN` 覆盖有没有生效，跑它最快：

  ```
  > python tools\_cores.py
  big cores: [0, 1, 10, 11, 12, 13]
  cores 0,1,10,11,12,13 (mask 0x3C03, pinned)
  ```

* `_toolchain.py` 单独跑会打印这台机器解析出来的完整工具链；解析错了，这里是第一个说话的地方
  （比 build 失败得不明不白要早）。覆盖方式：`CMAKE=` / `CXX=` / `NINJA=` / `CPU_MSYS2_ROOT=`
  （Windows 上换 MSYS2 根）：

  ```
  > python tools\_toolchain.py
  platform : windows
  toolchain: MSYS2 UCRT64 at D:\WindowsServer2025_1\software\MSYS2
  cmake    : D:\...\ucrt64\bin\cmake.exe
  ```

* 工具里**不要写死仓库绝对路径**。`ctest_stress.py` 是从 `.scratch/ctest_stress.py`
  提升上来的，那次做的事就是把三个硬编码常量（构建树、输出目录、MSYS2 根）换成从
  `__file__` 往上推 + `--tree` / `--out`。`.scratch/` 里的旧版本保持原样不动，它是
  `bridge/docs` 引用过的证据。
* 唯一的例外是 `_toolchain.py` 的 `DEFAULT_MSYS2`：那是**本机约定**而不是仓库路径，而且
  `CPU_MSYS2_ROOT` 一直能覆盖它（`bridge/windows/bridge_env.bat` 也有一套等价的默认值）。
  这个默认值只能有一个来源；再抄一份到别的脚本里就会开始漂。
