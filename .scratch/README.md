# .scratch · 实测证据与一次性探针

这里既不是垃圾桶也不是源码目录，它是这个仓库的**证据库**：`bridge/docs/` 里的
「证据 N」大多指向本目录下的某个文件 —— 一份跑批日志、一张地图、一个现场探针脚本。
所以规矩和别处相反：**没有被引用的可以归档，被引用的一律不许动。**

2026-09-17 整理过一轮：把没有被任何文档或源码引用的条目整体移进 `attic/`（保留原名，
不删除）。随时可以用 `python tools\find_scratch_refs.py` 重新算出「谁在被引用」，
用 `--check` 确认引用都还指得到东西。

上表是 2026-09-17 晚重新数过的一次：`find_scratch_refs.py` 报 **112** 个 `\\.scratch` 路径被 9 个
文件引用；顶层条目 277 = 文件 271 + 目录 6；`attic/` 3729（不含 `MANIFEST.txt`）。上一版写的
254 + 5 / 3729 是整理当晚的数，之后又落了十几个探针，所以对不上了。

## 当前构成

| 位置 | 条目数 | 是什么 |
| --- | --- | --- |
| 本层（文件 271 个 + 目录 6 个） | 277 | 被文档引用的证据 + 仍在用的脚本，**默认不动** |
| `attic/` | 3729 | 归档的一次性探针 / 日志 / 中间产物 / 旧二进制 / 生成物快照 |

`attic/` 里有一份 `MANIFEST.txt`，头几行就是整体还原的命令：

```
python -c "import os,shutil;b=r'E:\DesktopData\app\code\CPU\.scratch';a=os.path.join(b,'attic');[shutil.move(os.path.join(a,n),os.path.join(b,n)) for n in os.listdir(a) if n!='MANIFEST.txt']"
```

## 命名约定

| 形态 | 是什么 |
| --- | --- |
| `_foo.py` | 排障现场用的一次性探针 |
| `z1.py` … `z10.py` | 更早的一批同类探针 |
| `acceptance_*.json` / `.log` | `bridge/scripts/acceptance_check.py` 的历史跑批结果 |
| `ctest_stress.json` + `stress_fail_NNN.log` | `tools/ctest_stress.py` 的输出 |
| `*_ab.py` / `*_ab.out` / `*_ab.txt` | A/B 对照实验（改动前 vs 改动后） |
| `fm_*.json` / `fm_*.out` | 控制器选型对照（RPP / MPPI / warm）的落盘结果 |
| `slam_*.pgm` / `.png` / `.posegraph` / `.yaml` | SLAM 建图与存档实验 |
| `vo_*` | vector_object_server 的生命周期实验 |
| `gen_*`、`_gencheck`、`_before` | 生成器改动前后生成的代码 / 二进制快照，用来 diff |
| `wp*.json`、`wt*.log` | 某轮巡航 / 等待实验的记录 |

## 新增东西放哪里

* **能复用的工具** → `tools/`
* **一次性的探针、日志、证据** → 本目录顶层（不要直接塞 `attic/`，那是归档区）
* **大块构建产物** → 各自的 `build/`，不要放这里

写 `.py` 探针请用**无 BOM + LF**；写 `.md` 用 **BOM + LF**，并且跑一遍
`python tools\check_docs.py`。
