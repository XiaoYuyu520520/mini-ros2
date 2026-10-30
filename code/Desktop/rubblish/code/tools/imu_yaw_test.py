#!/usr/bin/env python3

import math
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Imu


class ImuYawCompare(Node):

    def __init__(self):
        super().__init__('imu_yaw_compare')

        self.sub = self.create_subscription(
            Imu,
            '/IMU_data',
            self.callback,
            100
        )

        self.duration = 30.0

        self.start_time = None
        self.last_time = None

        # orientation yaw
        self.last_orientation_yaw = None
        self.orientation_unwrapped = 0.0

        # gyro 积分 yaw
        self.gyro_yaw = 0.0

        # gyro z 统计
        self.gyro_z_sum = 0.0
        self.gyro_z_min = float('inf')
        self.gyro_z_max = float('-inf')
        self.sample_count = 0

        self.finished = False

        self.get_logger().info(
            '订阅 /IMU_data，比较 orientation yaw 与 gyro z 积分 yaw'
        )

    @staticmethod
    def quaternion_to_yaw(q):
        siny_cosp = 2.0 * (
            q.w * q.z +
            q.x * q.y
        )

        cosy_cosp = 1.0 - 2.0 * (
            q.y * q.y +
            q.z * q.z
        )

        return math.atan2(
            siny_cosp,
            cosy_cosp
        )

    @staticmethod
    def normalize_angle(angle):

        while angle > math.pi:
            angle -= 2.0 * math.pi

        while angle < -math.pi:
            angle += 2.0 * math.pi

        return angle

    def callback(self, msg):

        if self.finished:
            return

        now = self.get_clock().now().nanoseconds / 1e9

        orientation_yaw = self.quaternion_to_yaw(
            msg.orientation
        )

        gyro_z = msg.angular_velocity.z

        # ---------------------------
        # 第一帧
        # ---------------------------

        if self.start_time is None:

            self.start_time = now
            self.last_time = now

            self.last_orientation_yaw = orientation_yaw

            print()
            print('========== 开始 30 秒测试 ==========')

            print(
                f'初始 orientation yaw: '
                f'{math.degrees(orientation_yaw):+.6f}°'
            )

            print(
                f'初始 gyro_z: '
                f'{gyro_z:+.8f} rad/s '
                f'({math.degrees(gyro_z):+.6f} °/s)'
            )

            return

        # ---------------------------
        # dt
        # ---------------------------

        dt = now - self.last_time
        self.last_time = now

        if dt <= 0 or dt > 1.0:
            return

        elapsed = now - self.start_time

        # ===========================
        # 1. orientation yaw 解包
        # ===========================

        delta_orientation = (
            orientation_yaw -
            self.last_orientation_yaw
        )

        delta_orientation = self.normalize_angle(
            delta_orientation
        )

        self.orientation_unwrapped += delta_orientation

        self.last_orientation_yaw = orientation_yaw

        # ===========================
        # 2. gyro z 直接积分
        # ===========================

        self.gyro_yaw += gyro_z * dt

        # ===========================
        # gyro z 统计
        # ===========================

        self.gyro_z_sum += gyro_z
        self.sample_count += 1

        self.gyro_z_min = min(
            self.gyro_z_min,
            gyro_z
        )

        self.gyro_z_max = max(
            self.gyro_z_max,
            gyro_z
        )

        orientation_deg = math.degrees(
            self.orientation_unwrapped
        )

        gyro_deg = math.degrees(
            self.gyro_yaw
        )

        # 实时显示
        print(
            '\r'
            f't={elapsed:6.2f}s  '
            f'orientation={orientation_deg:+9.5f}°  '
            f'gyro_integral={gyro_deg:+9.5f}°  '
            f'gyro_z={math.degrees(gyro_z):+8.4f}°/s',
            end='',
            flush=True
        )

        # ===========================
        # 30 秒结束
        # ===========================

        if elapsed >= self.duration:

            self.finished = True

            avg_gyro_z = (
                self.gyro_z_sum /
                self.sample_count
            )

            avg_gyro_deg_s = math.degrees(
                avg_gyro_z
            )

            print('\n')

            print(
                '========== 30 秒测试结果 =========='
            )

            print(
                f'统计时间              : '
                f'{elapsed:.3f} s'
            )

            print()

            print(
                '--- orientation 姿态输出 ---'
            )

            print(
                f'orientation yaw 偏移   : '
                f'{orientation_deg:+.6f}°'
            )

            print()

            print(
                '--- gyro Z 原始积分 ---'
            )

            print(
                f'gyro 积分 yaw 偏移     : '
                f'{gyro_deg:+.6f}°'
            )

            print(
                f'gyro_z 平均值          : '
                f'{avg_gyro_z:+.9f} rad/s'
            )

            print(
                f'gyro_z 平均值          : '
                f'{avg_gyro_deg_s:+.6f} °/s'
            )

            print(
                f'gyro_z 最小值          : '
                f'{math.degrees(self.gyro_z_min):+.6f} °/s'
            )

            print(
                f'gyro_z 最大值          : '
                f'{math.degrees(self.gyro_z_max):+.6f} °/s'
            )

            print()

            print(
                '--- 差异 ---'
            )

            print(
                f'纯 gyro 与姿态 yaw 差 : '
                f'{gyro_deg - orientation_deg:+.6f}°'
            )

            print(
                '==================================='
            )

            rclpy.shutdown()


def main(args=None):

    rclpy.init(args=args)

    node = ImuYawCompare()

    try:
        rclpy.spin(node)

    except KeyboardInterrupt:
        pass

    finally:

        if rclpy.ok():
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()