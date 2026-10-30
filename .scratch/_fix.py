import io
s = io.open(".scratch/_s2.py", encoding="utf-8").read()
old = 'sub("]\\n\\ndef run(", "]\\n\\n\\ndef run(")\n\n'
assert s.count(old) == 1
io.open(".scratch/_s2.py", "w", encoding="utf-8", newline="").write(s.replace(old, ""))
print("patched")