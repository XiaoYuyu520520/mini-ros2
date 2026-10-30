#!/usr/bin/env python3
import os
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import PoseStamped
# 车上（Linux）用 termios/tty 把 tty 设成 raw，于是不用回车就能读到单个键；
# Windows 没有这两个模块，键盘换成 msvcrt，见 GoalPoseSubscriber.get_key()。
import sys
if os.name == 'nt':
    import msvcrt
else:
    import select, termios, tty
import csv
import codecs

class GoalPoseSubscriber(Node):
    def __init__(self):
        super().__init__('get_point')
        self.subscription = self.create_subscription(
            PoseStamped,
            '/goal_pose',
            self.pose_stamped_cb,
            10)
        self.subscription  # prevent unused variable warning
        self.points = []
        # 只有 Linux 那边需要先把 tty 存下来、每次读键前后切换 raw 模式。
        self.settings = None if os.name == 'nt' else termios.tcgetattr(sys.stdin)

    def pose_stamped_cb(self, msg):
        gx = msg.pose.position.x
        gy = msg.pose.position.y
        gz = msg.pose.orientation.z
        gw = msg.pose.orientation.w
        self.points.append([gx, gy, gz, gw])

    def get_key(self):
        if os.name == 'nt':
            # msvcrt 的 kbhit/getwch 本身就是「不等回车、读一个键」，不用切换 raw 模式。
            return msvcrt.getwch() if msvcrt.kbhit() else ''
        tty.setraw(sys.stdin.fileno())
        rlist, _, _ = select.select([sys.stdin], [], [], 0.1)
        if rlist:
            key = sys.stdin.read(1)
        else:
            key = ''
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, self.settings)
        return key

    def data_write_csv(self, file_name, datas):
        with codecs.open(file_name, 'w+', 'utf-8') as file_csv:
            writer = csv.writer(file_csv, delimiter=',', quoting=csv.QUOTE_MINIMAL)
            for data in datas:
                writer.writerow(data)
        self.get_logger().info('Data written to CSV successfully!')

def main(args=None):
    rclpy.init(args=args)
    node = GoalPoseSubscriber()
    out_csv = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out_test.csv')

    try:
        while rclpy.ok():
            rclpy.spin_once(node, timeout_sec=0.1)
            key = node.get_key()
            if key == 'f' or key == 'F':
                node.data_write_csv(out_csv, node.points)
            if key == '\x03':  # Ctrl+C to exit
                break
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()