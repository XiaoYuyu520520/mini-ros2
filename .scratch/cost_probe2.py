# -*- coding: utf-8 -*-
"""Global costmap readout: wall band and the footprint max-cost at candidate goals."""
import sys
import time

import rclpy
from rclpy.node import Node
from nav2_msgs.srv import GetCostmap

rclpy.init()
node = Node("cost_probe2")
cli = node.create_client(GetCostmap, "/global_costmap/get_costmap")
if not cli.wait_for_service(timeout_sec=10.0):
    print("PROBE no service", flush=True)
    sys.exit(1)
fut = cli.call_async(GetCostmap.Request())
end = time.monotonic() + 20.0
while not fut.done() and time.monotonic() < end:
    rclpy.spin_once(node, timeout_sec=0.01)
spec = fut.result().map
m = spec.metadata
res = m.resolution
data = list(spec.data)
ox = m.origin.position.x
oy = m.origin.position.y


def cost(x, y):
    mx = int((x - ox) / res)
    my = int((y - oy) / res)
    if not (0 <= mx < m.size_x and 0 <= my < m.size_y):
        return -1
    return data[my * m.size_x + mx]


def box_max(x, y):
    best = 0
    for i in range(-7, 8):
        for j in range(-4, 5):
            c = cost(x + 0.32 * i / 7.0, y + 0.18 * j / 4.0)
            best = max(best, c)
    return best


print("PROBE rows y=1.60/1.65/1.70/1.75 for x=0.60..1.50", flush=True)
for yy in (1.60, 1.65, 1.70, 1.75):
    row = " ".join("%.2f:%d" % (0.60 + 0.05 * k, cost(0.60 + 0.05 * k, yy)) for k in range(19))
    print("PROBE y=%.2f %s" % (yy, row), flush=True)

print("PROBE goal footprint max cost (0.64x0.36 box, yaw 0) at y=1.50:", flush=True)
for k in range(7):
    x = 1.00 + 0.05 * k
    print("PROBE  goal (%.2f,1.50) center=%d box_max=%d" % (x, cost(x, 1.50), box_max(x, 1.50)), flush=True)
