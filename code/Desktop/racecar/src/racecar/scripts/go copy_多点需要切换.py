#!/usr/bin/env python3

from nav2_simple_commander.robot_navigator import BasicNavigator
from geometry_msgs.msg import PoseStamped
from nav2_msgs.action import NavigateToPose
from nav2_simple_commander.robot_navigator import BasicNavigator, TaskResult

import rclpy
import time

def main():
    rclpy.init()  # 初始化ROS 2
    navigator = BasicNavigator()  # 创建导航器实例

    # 定义导航点
    waypoints = [
        {"x": 1.178, "y": -1.089},
        {"x": 2.531, "y": -2.070},
        {"x": 4.180, "y": -3.071}
    ]

    # 遍历导航点
    for idx, point in enumerate(waypoints):
        goal_pose = PoseStamped()  # 创建目标姿态消息
        goal_pose.header.frame_id = 'map'  # 设置参考坐标系为地图
        goal_pose.header.stamp = navigator.get_clock().now().to_msg()  # 设置时间戳
        goal_pose.pose.position.x = point["x"]  # 设置目标点的x坐标
        goal_pose.pose.position.y = point["y"]  # 设置目标点的y坐标
        goal_pose.pose.orientation.w = 1.0  # 设置默认的方向（朝向正北）

        print(f"Sending goal to point ({point['x']}, {point['y']})")  # 打印当前目标点
        navigator.goToPose(goal_pose)  # 发送导航请求

        # 等待导航任务启动
        while not navigator.isTaskComplete():
            feedback = navigator.getFeedback()  # 获取导航反馈
            if feedback and feedback.distance_remaining > 0.0:  # 确保反馈值是合理的
                print(f"Distance to goal: {feedback.distance_remaining:.2f} meters")
                break  # 任务已启动，跳出等待循环
            time.sleep(0.5)  # 等待0.5秒后再次检查

        # 等待导航任务完成或距离小于0.6米
        while not navigator.isTaskComplete():
            feedback = navigator.getFeedback()  # 获取导航反馈
            if feedback:
                print(f"Distance to goal: {feedback.distance_remaining:.2f} meters")
                if feedback.distance_remaining < 0.6:  # 如果距离小于0.6米
                    print(f"Skipping to next waypoint as distance to goal is less than 0.6 meters.")
                    navigator.cancelTask()  # 显式取消当前任务
                    time.sleep(1.0)  # 等待1秒，确保导航器状态更新
                    break  # 跳出当前导航点的等待循环
            time.sleep(0.5)  # 等待0.5秒后再次检查

        # 检查导航结果
        result = navigator.getResult()
        if result == TaskResult.SUCCEEDED:  # 使用TaskResult枚举
            print(f"Reached waypoint {idx + 1}: ({point['x']}, {point['y']})")  # 打印完成标志
        elif result == TaskResult.CANCELED:
            print(f"Goal to waypoint {idx + 1} was canceled!")
        elif result == TaskResult.FAILED:
            print(f"Goal to waypoint {idx + 1} failed!")
        else:
            print(f"Goal to waypoint {idx + 1} has an invalid return status!")

        # 清理导航器状态
        navigator.clearAllCostmaps()  # 清除代价地图
        time.sleep(1.0)  # 等待1秒，确保状态清理完成

    print("All waypoints completed!")  # 所有导航点完成后打印
    navigator.lifecycleShutdown()  # 关闭导航器

if __name__ == "__main__":
    main()
