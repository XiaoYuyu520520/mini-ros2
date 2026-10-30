#!/usr/bin/env python3

import os
import sys

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from nav2_msgs.action import NavigateThroughPoses
from geometry_msgs.msg import PoseStamped, Twist
import csv
from std_srvs.srv import Trigger  # 导入 Trigger 服务

class NavThroughPosesClient(Node):
    def __init__(self):
        super().__init__('nav_through_poses_client')
        self._action_client = ActionClient(self, NavigateThroughPoses, 'navigate_through_poses')
        self.cmd_vel_sub = self.create_subscription(Twist, '/cmd_vel', self.cmd_vel_callback, 10)
        self.car_cmd_vel_pub = self.create_publisher(Twist, '/car_cmd_vel', 10)
        self.distance_remaining = None
        self.is_paused = False  # 标志位，用于控制是否暂停导航
        self.cycle_number=0
        self.fin_flag=False

    def send_goal(self, poses):
        goal_msg = NavigateThroughPoses.Goal()
        # goal 的 poses 两边写法不同：兼容层里移植的 nav2 (1.5.1) 把它定义成
        # nav_msgs/Goals，航点装在这个消息的 .goals 里；Jazzy 上真正的 nav2 定义成
        # geometry_msgs/PoseStamped[]，本身就是那串航点。两边都留着，谁长着 goals 就填谁。
        if hasattr(goal_msg.poses, "goals"):
            goal_msg.poses.goals = poses
        else:
            goal_msg.poses = poses

        self._action_client.wait_for_server(timeout_sec=10.0)
        self._send_goal_future = self._action_client.send_goal_async(goal_msg, feedback_callback=self.feedback_callback)
        self._send_goal_future.add_done_callback(self.goal_response_callback)

    def goal_response_callback(self, future):
        goal_handle = future.result()
        if not goal_handle.accepted:
            self.get_logger().info('Goal rejected :(')
            return

        self.get_logger().info('Goal accepted :)')

        self._get_result_future = goal_handle.get_result_async()
        self._get_result_future.add_done_callback(self.get_result_callback)

    def get_result_callback(self, future):
        result = future.result().result
        self.get_logger().info('Navigation result: {0}'.format(result))
        # rclpy.shutdown()

    def feedback_callback(self, feedback_msg):
        feedback = feedback_msg.feedback
        if  feedback.distance_remaining > 0.0:
            self.get_logger().info(f'Distance to target: {feedback.distance_remaining:.2f} meters')
            self.distance_remaining = feedback.distance_remaining
            # 如果距离目标点的距离小于 1 米，发送停止命令
            if self.distance_remaining is not None and feedback.distance_remaining < 0.5 and not self.is_paused:


                self.get_logger().info('Reached the target, stopping the robot.')
                self.is_paused = True  # 设置标志位为暂停状态
                twist = Twist()
                twist.linear.x = 0.0
                twist.angular.z = 0.0
                self.car_cmd_vel_pub.publish(twist)


    def cmd_vel_callback(self, msg):

        # if  self.distance_remaining > 0.5 and not self.is_paused:
        if self.distance_remaining is not None and self.distance_remaining > 0.5 and not self.is_paused:
            # 否则，正常转发速度命令
            self.car_cmd_vel_pub.publish(msg)

        

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
    # 这一趟跑哪条航线：`run go.py sim_room12.csv` 会给一个文件名，不给就用脚本
    # 自带的那条赛道航线。名字相对脚本目录解析，和车上、机器上都一致。
    script_dir = os.path.dirname(os.path.abspath(__file__))
    named = [a for a in sys.argv[1:] if a.lower().endswith('.csv')]
    if not named:
        return os.path.join(script_dir, 'ai_test.csv')
    return named[0] if os.path.isabs(named[0]) else os.path.join(script_dir, named[0])


def rclpy_argv():
    # 这个脚本的航点文件是个裸参数，rclpy 不认识它（真车上 `ros2 run racecar go.py FILE`
    # 会把 FILE 当成解析不了的全局参数），所以裸参数不进 rclpy；带 `-` 的照旧传进去。
    # 但 `-p use_sim_time:=true` 是「选项 + 它的值」，只留带 `-` 的那半会剩下一个没值的
    # `-p`（兼容层的 rclpy 会安静地丢掉它，于是脚本留在墙上时间上）。所以 `--ros-args`
    # 往后原样透传，让 -p/-r 的取值跟着走。
    argv = sys.argv
    tail = []
    if '--ros-args' in argv:
        cut = argv.index('--ros-args')
        tail, argv = argv[cut:], argv[:cut]
    return [argv[0]] + [a for a in argv[1:] if a.startswith('-')] + tail


def main(args=None):
    rclpy.init(args=rclpy_argv())

    client = NavThroughPosesClient()

    waypoints = read_waypoints_from_csv(waypoint_file())


    # 将导航点转换为 PoseStamped 消息
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