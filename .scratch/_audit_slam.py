import io, os
ROOT = "E:/DesktopData/app/code/CPU/nav2/slam_toolbox_port"
for dp, dn, fn in os.walk(ROOT):
    if "build" in dp.split(os.sep):
        continue
    rel = os.path.relpath(dp, ROOT)
    for f in sorted(fn):
        print("%-44s %8d" % (os.path.join(rel, f), os.path.getsize(os.path.join(dp, f))))