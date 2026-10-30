#!/usr/bin/env python3
# 按 CSV 的航线一圈一圈跑，跑够圈数为止。
#
# 这套 nav2 上「一圈一圈跑」要交给 waypoint_follower 的 `FollowWaypoints`：
# Goal.number_of_loops 让同一串航点重跑，字段从 0 数起，N 就是 N+1 遍
# （nav2_waypoint_follower/src/waypoint_follower.cpp:357-372；实测
# `.scratch/probe_follow_loops.py`：number_of_loops=1 走两遍、4167 条 feedback）。
#
# 不能再用「跑完一遍就把同一串 PoseStamped 再发一次 navigate_through_poses」：
# controller_server 的 goal checker 只看**路径终点**
# （src/racecar/config/nav.yaml:138 的 xy_goal_tolerance: 1.0），车还停在上趟终点，
# 第一拍就是 Reached the goal —— 0.08 s 返回一个「成功」，车一动不动。实测见
# bridge/docs/03-validation.md 证据 78。
#
#     ros2 run racecar 重复载入完成圈数.py [sim_room12.csv] [--laps N]
#     racecar_ctl.py run 重复载入完成圈数.py sim_room12.csv --laps 2      # 兼容层上同一条


import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from nav2_msgs.action import FollowWaypoints
from geometry_msgs.msg import PoseStamped, Twist
import csv
import os
import sys

# 目标圈数：这份原来是 target_laps = 3（`--laps N` 改）。
DEFAULT_LAPS = 3


def laps_argument(argv):
    """`--laps N`（或 `--laps=N`）从命令行里拿出来：((剩下的参数), 圈数或 None)。

    这个参数是我们自己的，不能留给 rclpy 去解析，所以 main() 把它摘掉之后再
    `rclpy.init()`；航线名照旧只由 waypoint_file() 从 sys.argv 里挑。
    """
    rest, laps, index = [], None, 0
    while index < len(argv):
        arg = argv[index]
        if arg.startswith("--laps="):
            laps = int(arg.split("=", 1)[1])
        elif arg == "--laps" and index + 1 < len(argv):
            index += 1
            laps = int(argv[index])
        else:
            rest.append(arg)
        index += 1
    return rest, laps


class FollowWaypointsClient(Node):
    def __init__(self, laps):
        super().__init__('follow_waypoints_client')
        self._action_client = ActionClient(self, FollowWaypoints, 'follow_waypoints')
        # 控制器发的是 /cmd_vel，底盘驱动收的是 /car_cmd_vel（
        # src/racecar/CMakeLists.txt:52-53 记着这两个单位口径），这个节点是两者之间
        # 的那一段：仿真里 /car_cmd_vel 没有订阅者，真车上没有它就没人给底盘发命令。
        self.cmd_vel_sub = self.create_subscription(Twist, '/cmd_vel', self.cmd_vel_callback, 10)
        self.car_cmd_vel_pub = self.create_publisher(Twist, '/car_cmd_vel', 10)
        self.laps = max(1, laps)          # number_of_loops 从 0 数起，见下面的 -1
        self.poses = []
        self.reported_waypoint = None
        self.moving = True

    def send_goal(self, poses):
        self.poses = poses
        goal_msg = FollowWaypoints.Goal()
        goal_msg.poses = poses
        # 跑 `laps` 遍 = number_of_loops 给 laps - 1（0 就是一趟）
        goal_msg.number_of_loops = self.laps - 1

        self._action_client.wait_for_server(timeout_sec=10.0)
        self._send_goal_future = self._action_client.send_goal_async(
            goal_msg, feedback_callback=self.feedback_callback)
        self._send_goal_future.add_done_callback(self.goal_response_callback)

    def goal_response_callback(self, future):
        goal_handle = future.result()
        if not goal_handle.accepted:
            self.get_logger().info('Goal rejected :(')
            rclpy.shutdown()
            return

        self.get_logger().info('Goal accepted :)  %d pose(s), %d lap(s)'
                               % (len(self.poses), self.laps))

        self._get_result_future = goal_handle.get_result_async()
        self._get_result_future.add_done_callback(self.get_result_callback)

    def get_result_callback(self, future):
        result = future.result().result
        # 结果面在 Humble 和 Jazzy 之间换过：Humble 有 navigation_time，Jazzy（这套
        # 兼容层也是）用 error_code 代替它（navigation_time 现在只在 feedback 里）。
        # 两边都能读，0 就是成功。
        missed = list(getattr(result, 'missed_waypoints', []))
        error_code = getattr(result, 'error_code', 0)
        self.get_logger().info('Navigation result: error_code=%d missed_waypoints=%d'
                               % (error_code, len(missed)))
        # 圈数跑完了：给底盘一条零速，然后退出。原来的写法在这里只打一行日志，进程
        # 一直挂着不退 —— 上一轮那批遗留进程（11 个脚本各 2 个）就是这么留下的。
        self.stop_robot()
        rclpy.shutdown()

    def feedback_callback(self, feedback_msg):
        # FollowWaypoints 的 feedback 只有 current_waypoint（没有
        # distance_remaining），所以这里只报「第几个点」。
        current = feedback_msg.feedback.current_waypoint
        if current != self.reported_waypoint:
            self.reported_waypoint = current
            self.get_logger().info('Waypoint %d/%d' % (current + 1, len(self.poses)))

    def cmd_vel_callback(self, msg):
        if self.moving:
            self.car_cmd_vel_pub.publish(msg)

    def stop_robot(self):
        self.moving = False
        self.car_cmd_vel_pub.publish(Twist())


def read_waypoints_from_csv(filename):
    waypoints = []
    with open(filename, 'r') as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) == 4:  # Ensure each row has x, y, z, w
                waypoints.append({
                    "x": float(row[0]),
                    "y": float(row[1]),
                    "z": float(row[2]),
                    "w": float(row[3])
                })
    return waypoints


def waypoint_file():
    # 这一趟跑哪条航线：`run <这个脚本> sim_room12.csv` 会给一个文件名，不给就用脚本目录里的
    # test.csv（车上原本那份），车上没放、仓库里也没有才退回随包的 ai_test.csv。名字一律相对
    # 脚本目录解析，于是车上、这台机器上、仓库里的任何位置跑都指同一个文件。
    script_dir = os.path.dirname(os.path.abspath(__file__))
    named = [a for a in sys.argv[1:] if a.lower().endswith('.csv')]
    if named:
        return named[0] if os.path.isabs(named[0]) else os.path.join(script_dir, named[0])
    for fallback in ('test.csv', 'ai_test.csv'):
        candidate = os.path.join(script_dir, fallback)
        if os.path.exists(candidate):
            return candidate
    return os.path.join(script_dir, 'ai_test.csv')


def main(args=None):
    argv, laps = laps_argument(sys.argv[1:])
    if laps is None:
        laps = DEFAULT_LAPS
    # 把 --laps 摘掉再交给 rclpy，其余参数（航线名、真车上的 --ros-args）原样传下去。
    rclpy.init(args=[sys.argv[0]] + argv)

    client = FollowWaypointsClient(laps)

    waypoints = read_waypoints_from_csv(waypoint_file())

    # 将导航点转换为 PoseStamped 消息。CSV 的第 3、4 列是四元数的 z 和 w，不是
    # position.z 和 w —— 原来的写法把朝向丢了（每个点都变成「朝 +x」），
    # 实测见 bridge/docs/03-validation.md 证据 80。
    poses = []
    for point in waypoints:
        pose = PoseStamped()
        pose.header.frame_id = 'map'
        pose.header.stamp.sec = 0
        pose.pose.position.x = point["x"]
        pose.pose.position.y = point["y"]
        pose.pose.orientation.z = point["z"]
        pose.pose.orientation.w = point["w"]
        poses.append(pose)

    client.send_goal(poses)

    rclpy.spin(client)


if __name__ == '__main__':
    main()
