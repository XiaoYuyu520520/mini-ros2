import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8")
import big6

BUILD = r"E:\DesktopData\app\code\CPU\bridge\windows\build"
jobs = int(os.environ.get("BUILD_JOBS", "3"))
sys.exit(big6.build(BUILD, sys.argv[1:], jobs=jobs))
