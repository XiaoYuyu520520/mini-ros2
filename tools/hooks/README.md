# tools/hooks · Claude Code 的编辑前检查

这里放给 Claude Code 用的钩子脚本。钩子本身要在 `.claude/settings.json` 里登记，
这个目录只负责「拿到一次工具调用，说该不该放行」。

| 文件 | 事件 | 做什么 |
| --- | --- | --- |
| `check_md_write.py` | `PreToolUse`（`Edit` / `Write` / `MultiEdit`） | 目标是 `.md` 时，把这次修改**将要写出的字节**交给 `tools/check_docs.py` 的 `check_bytes()` 判一遍；有错误（CR 字节、控制字符、不成对的 ``` 围栏）就拒绝，只有警告（缺 BOM）就放行并在状态栏提示 |

判定逻辑只有一份：`check_docs.py` 读文件和这个钩子判「改完之后」走的是同一个
`check_bytes()`，所以钩子与 `python tools/check_docs.py --quiet` 不会给出不同答案。

## 启用（项目级，提交到仓库）

在仓库根的 `.claude/settings.json` 里加：

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "Edit|Write|MultiEdit",
        "hooks": [
          {
            "type": "command",
            "command": "cd \"$CLAUDE_PROJECT_DIR\" && py -3 -I tools/hooks/check_md_write.py",
            "timeout": 60,
            "statusMessage": "Checking .md encoding"
          }
        ]
      }
    ]
  }
}
```

`py -3` 是这台机器上 Python 的启动方式（见根 `CLAUDE.md`）；其它机器改成 `python3` 即可。

## 单独试一下

钩子从 stdin 读工具调用的 JSON，把决定打到 stdout：

```bash
py -3 -I tools/hooks/check_md_write.py < hook-input.json
```

一行正常修改没有任何输出；带 CR 的修改输出 `"permissionDecision": "deny"`；
非 `.md` 目标直接放行、不输出。退出码永远是 0 —— 钩子挂掉不应该挡住编辑，
`check_docs.py` 在 CI 里仍会把问题报出来。
