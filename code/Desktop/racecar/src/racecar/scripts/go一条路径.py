#!/usr/bin/env python3

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from nav2_msgs.action import NavigateThroughPoses
from geometry_msgs.msg import PoseStamped

class NavThroughPosesClient(Node):
    def __init__(self):
        super().__init__('nav_through_poses_client')
        self._action_client = ActionClient(self, NavigateThroughPoses, 'navigate_through_poses')

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
        rclpy.shutdown()

    def feedback_callback(self, feedback_msg):
        feedback = feedback_msg.feedback
        self.get_logger().info('Received feedback: {0}'.format(feedback))

def main(args=None):
    rclpy.init(args=args)

    client = NavThroughPosesClient()

    # 定义导航点
    waypoints = [
        {"x": 2.67, "y": -0.11},
        {"x": 4.84, "y": 0.84},
        {"x": 2.05, "y": 2.48}
        # {"x": -1.16, "y": -1.17}
        # {"x": 0.00, "y": 0.00}

    ]

    # 将导航点转换为 PoseStamped 消息
    poses = []
    for point in waypoints:
        pose = PoseStamped()
        pose.header.frame_id = 'map'
        pose.header.stamp.sec = 0
        pose.pose.position.x = point["x"]
        pose.pose.position.y = point["y"]
        pose.pose.orientation.w = 1.0
        poses.append(pose)

    client.send_goal(poses)

    rclpy.spin(client)

if __name__ == '__main__':
    main()
