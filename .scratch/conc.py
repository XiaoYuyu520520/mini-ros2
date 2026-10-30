# -*- coding: utf-8 -*-
import io, sys
sys.stdout.reconfigure(encoding="utf-8")
t = io.open(r"E:\DesktopData\app\code\CPU\bridge\docs\03-validation.md", encoding="utf-8").read()
a = t.find("## 验收结论")
b = t.find("## 一键复现")
print(t[a:b])