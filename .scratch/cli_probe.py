import re
p="bridge/windows/tools/ros2_cli.cpp"
t=open(p,encoding="utf8").read()
print("lines",t.count("\n")+1,"bytes",len(t))
print("--- comparisons ---")
for m in re.finditer(r'(?:cmd|verb|sub|command|mode)\w*\s*==\s*"([a-zA-Z_]+)"',t):
    print("CMP",m.group(1))
print("--- usage/help block ---")
for m in re.finditer(r'"([a-z][a-z_]{2,18})"',t):
    pass
# find the dispatch function
i=t.find("dispatch")
print("dispatch idx",i)