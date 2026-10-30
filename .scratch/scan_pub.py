import re
p = r"E:\DesktopData\app\code\CPU\bridge\windows\tools\ros2_cli.cpp"
s = open(p, encoding="utf-8", errors="replace").read().splitlines()
start = None
for i, l in enumerate(s, 1):
    if re.search(r'topic pub|cmdTopicPub|parseAssignments|k=v|"pub"', l):
        print(i, l.rstrip()[:150])
