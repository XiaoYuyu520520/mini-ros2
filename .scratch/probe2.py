import pathlib
p = pathlib.Path(r"E:\DesktopData\app\code\CPU\nav2\nav2_extracted\navigation2-1.5.1\nav2_bringup\launch\navigation_launch.py")
lines = p.read_text(encoding="utf-8").splitlines()
for i, l in enumerate(lines[40:60], 41):
    print(i, l.rstrip())
print('---')
for i, l in enumerate(lines[165:275], 166):
    print(i, l.rstrip())