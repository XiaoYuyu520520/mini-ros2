---
name: ci-release-github
description: Use when publishing this repo to GitHub - setting up Actions for a Windows and a Linux build, producing release archives, or debugging a job that passes on the development machine and fails on a runner. Covers the "local has it, clean checkout does not" class of failures, the fetch script for vendored sources, and how to push without putting a token on disk.
---

# 上架 GitHub：双平台 CI + 发布包

## 现状

- 仓库：https://github.com/XiaoYuyu520520/mini-ros2.git（`main`，MIT LICENSE）
- `.github/workflows/cross-platform.yml`：**测试门禁**（push / PR）
- `.github/workflows/release.yml`：**建包 + 发布**（tag `v*` 发 Release，
  其他触发只出 artifact）
- 包名由 `package.py` 唯一定义：
  `mini-ros2-windows-x86_64-<ver>.zip` / `mini-ros2-linux-x86_64-<ver>.tar.gz`

两个 workflow 的步骤顺序**一样**，因为「两个平台跑同一串命令」本身就是门禁：

```
checkout → setup-python → fetch_deps → pip pyyaml → [msys2 | apt] →
工具链解析 → 四棵树 build → 五棵树 test → 四个门禁 → package.py
```

## 推送而不把 token 落盘

```bash
git config --local http.proxy  http://127.0.0.1:10090
git config --local https.proxy http://127.0.0.1:10090

export GITHUB_TOKEN='<pat>'
git -c credential.helper='!f() { echo username=x-access-token; echo "password=$GITHUB_TOKEN"; }; f' \
    push origin main
```

token 只在环境变量里，不进 argv、不进 `.git/config`、不进仓库。
**推完之后把 token 吊销**（它出现在对话/日志里过）。

## 「本地有、干净 checkout 没有」——runner 上最常见的一类失败

第一次在真 runner 上跑，7 个问题**全部**是这一类。清单：

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| `fetch` 步骤：文件不存在 | 整个目录被 gitignore，**脚本自己也没推上去** | 忽略内容、保留脚本（`dir/*` + `!dir/*.py`） |
| 依赖下载 404 | 上游 ref 写错（`yaml-cpp-0.8.0` 是本地目录名，上游 tag 是 `0.8.0`） | 按上游真实 ref；压缩包顶层目录名是 `<repo>-<ref>`，按前缀匹配 |
| 工具链找不到 | 写死了工具链安装路径 | 用 action 的输出（`steps.<id>.outputs.<name>`） |
| ctest 目录不存在 | 那棵树**从没被单独 configure 过** | 补一个 build 步骤 |
| `ModuleNotFoundError` | 构建脚本在模块加载时 import 了一个 pip 包 | workflow 里补 `pip install` |
| 引用路径「不存在」 | 被引用的文件**故意不入库**（归档区、生成物） | 门禁加"已知不入库"前缀，只跳存在性检查 |
| 编译错误只在 Linux 出现 | POSIX 符号在 Windows 分支里编不到 | CI 上才暴露，改代码 |

**排查动作**：先问「这个文件/目录在**干净 checkout** 里存在吗」。
本地构建产物、被 ignore 的归档、Windows-only 的分支，都是这类问题的来源。

## 取 CI 日志

API 的 `/actions/jobs/<id>/logs` 会 **302 到 blob 存储**；用 urllib 跟随后
`Authorization` 头被带到新域 → **403**。用 curl（默认不把自定义头转发到别的主机）：

```bash
curl -sSL --max-time 180 -H "Authorization: token $GITHUB_TOKEN" -H "User-Agent: chk" \
  "https://api.github.com/repos/<owner>/<repo>/actions/jobs/<job_id>/logs" -o job.log
# job.log 是 zip，解开看每个 step 的输出
```

`/actions/runs/<id>/jobs` 能拿到**每个 step 的 conclusion** —— 哪个步骤挂了
一眼就能看到，不用日志。

## 构建要多久

两个平台各约 **25–30 分钟**（nav2 511 个目标，2 核）。所以：

- **一次改动只推一次**，别推一串中间版本（每个都是冷编译）
- 想让后续迭代变快，就给 `nav2/ros2_shim/build`、`bridge/windows/build`、
  `gRPC/ros2_gRPC_sdk/build` 和 `nav2/third_party/dl` 加 `actions/cache`
  （key 用源码哈希，workflow 改动不影响命中）

## 打包

`package.py` 是**唯一**决定包内容的东西，CI 只调用它：

```bash
python package.py --list                     # 只列清单
python package.py --platform windows         # 打 Windows 包
python package.py --version 0.1.0 --force
```

- **保持仓库相对路径**（`bridge/windows/build/`、`nav2/ros2_shim/build/`、
  `nav2/nav2_extracted/navigation2-1.5.1/`）—— 拍平会让脚本找不到文件
- 只收**运行需要**的东西：`apps/` 对应的服务器 + 少量诊断工具 + 桥的 exe 和
  **88 个 DLL** + 配置 + `nav2_extracted` 里的 params 和 behavior_trees（58 KB）
- `test_*` / `bench_*` 不收（`--with-tests` 才收）
- zip 用固定时间戳 → 同样构建产出同样字节

## 打完包要验什么

不能只信「包生成了」：

1. `--list` 看清单和体积
2. 检查脚本会拼的运行时路径**在包里存在**
3. **解压到干净目录**，从解压后的树直接跑二进制（Windows 包不需要 MSYS2，
   DLL 都在包里）
