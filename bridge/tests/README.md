# bridge\tests · 只有基准数据，没有测试代码

这个目录**不放测试**：桥的 5 条 ctest 源码在 `../windows/tests/`。这里放的是 `cdr_check` 用的
基准字节。

| 路径 | 作用 |
| --- | --- |
| `fixtures/<名字>.cdr` | ROS2 线上 CDR 字节，由 `rosbags` 的类型库产生 |
| `fixtures/<名字>.shim` | 同一个消息在兼容层共享内存里的布局，从 shim 自己的生成头解析出来 |
| `fixtures/manifest.tsv` | 名字 → 消息类型，`cdr_check` 按它遍历 |
| `generate_fixtures.py` | 重新生成上面这些（两个独立实现各出一份，所以字节一致才有意义） |

路径由 `../windows/CMakeLists.txt` 用 `-DROS2_BRIDGE_FIXTURE_DIR` 传给 `bridge_cdr_check`；
逐 fixture 的结果见 `../docs/03-validation.md`。
