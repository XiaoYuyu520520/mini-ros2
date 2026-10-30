#!/usr/bin/env python3

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from nav2_msgs.action import NavigateThroughPoses
from geometry_msgs.msg import PoseStamped
import csv
import os
import sys
from std_srvs.srv import Trigger  # 导入 Trigger 服务


class NavThroughPosesClient(Node):
    def __init__(self):
        super().__init__('nav_through_poses_client')
        self._action_client = ActionClient(self, NavigateThroughPoses, 'navigate_through_poses')
        self._stop_client = None
        self._stop_missing = False

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
        # time.sleep(2)
        rclpy.shutdown()

    def feedback_callback(self, feedback_msg):
        feedback = feedback_msg.feedback
        if feedback.distance_remaining > 0.0:
            self.get_logger().info(f'Distance to target: {feedback.distance_remaining:.2f} meters')
            # 如果距离目标点的距离小于 0.5 米，调用 stop 服务
            if feedback.distance_remaining < 1:
                self.get_logger().info('Reached the target, stopping the robot.')
                self.call_stop_service()

    def call_stop_service(self):
        # 车上有个发 `stop` 的节点；这套兼容层里没有，问到就问不到。原来的写法是
        # `while not wait_for_service(...)`，而这里已经在反馈回调里了 —— 服务不在就
        # 每个反馈都卡满一轮，永远出不来。所以改成只问一次、记下来就不再问。
        if self._stop_missing:
            return
        if self._stop_client is None:
            self._stop_client = self.create_client(Trigger, 'stop')
        if not self._stop_client.wait_for_service(timeout_sec=5.0):
            self._stop_missing = True
            self.get_logger().warn('no `stop` service in this graph: the robot is not being stopped')
            return
        request = Trigger.Request()
        future = self._stop_client.call_async(request)
        future.add_done_callback(self.stop_response_callback)

    def stop_response_callback(self, future):
        response = future.result()
        if response.success:
            self.get_logger().info('Robot stopped successfully.')
        else:
            self.get_logger().info('Failed to stop the robot.')

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
    rclpy.init(args=args)

    client = NavThroughPosesClient()

    # 从CSV文件读取导航点
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