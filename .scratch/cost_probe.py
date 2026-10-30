# -*- coding: utf-8 -*-
"""Read /global_costmap/get_costmap and print the cost around the probe points."""
import math
import sys
import time

import rclpy
from rclpy.node import Node
from nav2_msgs.srv import GetCostmap

POINTS = [(0.00, 0.00), (0.20, 0.20), (2.00, -1.00), (0.50, 2.00)]

rclpy.init()
node = Node("cost_probe")
cli = node.create_client(GetCostmap, "/global_costmap/get_costmap")
print("PROBE waiting", flush=True)
if not cli.wait_for_service(timeout_sec=10.0):
    print("PROBE no service", flush=True)
    sys.exit(1)
req = GetCostmap.Request()
fut = cli.call_async(req)
end = time.monotonic() + 20.0
while not fut.done() and time.monotonic() < end:
    rclpy.spin_once(node, timeout_sec=0.01)
if not fut.done():
    print("PROBE timeout", flush=True)
    sys.exit(1)
res = fut.result()
spec = res.map
m = spec.metadata
print("PROBE resolution=%.4f size=%dx%d origin=(%.2f,%.2f)" % (
    m.resolution, m.size_x, m.size_y, m.origin.position.x, m.origin.position.y), flush=True)
data = list(spec.data)

def cost(x, y):
    mx = int((x - m.origin.position.x) / m.resolution)
    my = int((y - m.origin.position.y) / m.resolution)
    if not (0 <= mx < m.size_x and 0 <= my < m.size_y):
        return -99
    return data[my * m.size_x + mx]

for p in POINTS:
    print("PROBE cost at (%.2f,%.2f) = %d" % (p[0], p[1], cost(*p)), flush=True)

# what is the lethal band around the wall corner at y=1.50 ?
row = []
for i in range(0, 40):
    x = 0.90 + 0.02 * i
    row.append("%.2f:%d" % (x, cost(x, 1.50)))
print("PROBE y=1.50 " + " ".join(row), flush=True)
