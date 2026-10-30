# -*- coding: utf-8 -*-
import rclpy
rclpy.init()
from rclpy.node import Node
n = Node("probe_svc_names")
names = sorted(n.get_service_names_and_types())
print("count:", len(names))
for name, types in names:
    print("%-58s %s" % (name, types))
