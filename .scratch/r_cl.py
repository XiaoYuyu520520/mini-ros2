import io
p = r"E:\DesktopData\app\code\CPU\nav2\ros2_shim\include\rclcpp\client.hpp"
lines = io.open(p, encoding="utf-8").read().split("\n")
for i in range(0, 115):
    print("%4d %s" % (i+1, lines[i]))