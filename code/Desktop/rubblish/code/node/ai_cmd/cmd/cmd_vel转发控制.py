#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""

cd /root/Desktop/code/node/testom3/cmd/

conda deactivate

source ~/Desktop/racecar/install/setup.bash

python3 cmd_vel转发控制.py

"""

import sys
import threading
import termios
import tty
import time
import struct
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
from enum import Enum
from multiprocessing import shared_memory, resource_tracker

# ============================================================
# 可配置参数配置区
# ============================================================

# ----------------------------
# ROS 2 控制节点配置
# ----------------------------
HIJACK_KEY = ' '           # 手动劫持快捷键（默认空格）
IN_TOPIC = '/cmd_vel'      # 订阅的上游控制话题
OUT_TOPIC = '/car_cmd_vel' # 实际发布给底盘的话题

# ----------------------------
# 共享内存与 YOLO 配置 (与显示端完全一致)
# ----------------------------
RESULT_SHM_NAME = "yolo_result_shm"
CLASS_NAMES = ["green", "red", "line"]
MAX_DETECTIONS = 32

# 置信度阈值
RED_CONF_THRESHOLD = 0.25
GREEN_CONF_THRESHOLD = 0.25
CROSSWALK_CONF_THRESHOLD = 0.50

# 标签匹配
RED_LABELS = {"red", "cv_red"}
GREEN_LABELS = {"green", "cv_green"}
CROSSWALK_LABELS = {"line", "crosswalk", "stop_line"}

# ----------------------------
# 车辆自动驾驶规则参数
# ----------------------------
CROSSWALK_STOP_SECONDS = 3.5                  # 斑马线/停止线停车时长
CROSSWALK_RETRIGGER_SUPPRESS_SECONDS = 15.0   # 斑马线停车完成后抑制重复触发的时间
RED_LIGHT_MAX_WAIT_SECONDS = 15.0             # 红灯最多等待时间
GREEN_CONFIRM_FRAMES = 3                      # 绿灯连续确认帧数

# ----------------------------
# 系统循环配置
# ----------------------------
SHM_RETRY_INTERVAL = 0.5
LOOP_SLEEP_SECONDS = 0.005
# 共享内存长时间无新数据则判定推理程序重启，重新连接
SHM_STALE_SECONDS = 3.0

# ============================================================
# 二进制结构配置 (与显示端完全一致)
# ============================================================
RESULT_HEADER_FMT = "<QQdIQ"
DET_FMT = "<iiiifI"
RESULT_HEADER_SIZE = struct.calcsize(RESULT_HEADER_FMT)
DET_SIZE = struct.calcsize(DET_FMT)

# ============================================================
# 状态与工具函数
# ============================================================

class VehicleState(Enum):
    GO = "GO"
    STOP_RED = "STOP_RED"
    STOP_CROSSWALK = "STOP_CROSSWALK"

def unregister_from_resource_tracker(shm):
    try:
        resource_tracker.unregister(shm._name, "shared_memory")
    except Exception:
        pass

def attach_shared_memory_no_unlink(name):
    shm = shared_memory.SharedMemory(name=name)
    unregister_from_resource_tracker(shm)
    return shm

# ===================== 核心修改：使用新的读取方法 =====================
def read_results(result_shm):
    """与显示端完全一致的检测结果读取函数"""
    header1 = struct.unpack_from(RESULT_HEADER_FMT, result_shm.buf, 0)
    seq1, _, infer_fps, det_count, camera_seq = header1
    if seq1 == 0 or seq1 % 2 == 1:
        return None, None, None, None

    det_count = min(int(det_count), MAX_DETECTIONS)
    detections = []
    offset = RESULT_HEADER_SIZE
    for _ in range(det_count):
        x1, y1, x2, y2, conf, cls_id = struct.unpack_from(DET_FMT, result_shm.buf, offset)
        detections.append((int(x1), int(y1), int(x2), int(y2), float(conf), int(cls_id)))
        offset += DET_SIZE

    header2 = struct.unpack_from(RESULT_HEADER_FMT, result_shm.buf, 0)
    seq2 = int(header2[0])
    if seq1 != seq2 or seq2 % 2 == 1:
        return None, None, None, None

    return detections, seq2 // 2, float(infer_fps), int(camera_seq)

def extract_signals(detections):
    """从新格式检测结果中提取红绿灯、斑马线信号"""
    red_conf, green_conf, crosswalk_conf = 0.0, 0.0, 0.0

    for x1, y1, x2, y2, conf, cls_id in detections:
        # 获取类别标签
        if 0 <= cls_id < len(CLASS_NAMES):
            label = CLASS_NAMES[cls_id].lower()
        else:
            continue
            
        conf = float(conf)
        if label in RED_LABELS and conf > red_conf:
            red_conf = conf
        elif label in GREEN_LABELS and conf > green_conf:
            green_conf = conf
        elif label in CROSSWALK_LABELS and conf > crosswalk_conf:
            crosswalk_conf = conf

    return {
        "red_seen": red_conf >= RED_CONF_THRESHOLD,
        "green_seen": green_conf >= GREEN_CONF_THRESHOLD,
        "crosswalk_seen": crosswalk_conf >= CROSSWALK_CONF_THRESHOLD,
    }

# ============================================================
# 车辆决策逻辑控制器
# ============================================================

class VehicleController:
    def __init__(self, logger):
        self.logger = logger
        self.state = VehicleState.GO
        self.last_state = VehicleState.GO
        
        self.crosswalk_stop_until = 0.0
        self.crosswalk_ignore_until = 0.0
        
        self.red_light_used = False
        self.red_stop_start_time = 0.0
        self.green_seen_frames = 0

    def is_stopped(self):
        return self.state in {VehicleState.STOP_RED, VehicleState.STOP_CROSSWALK}

    def _update_green_confirm(self, green_seen):
        if green_seen:
            self.green_seen_frames += 1
        else:
            self.green_seen_frames = 0
        return self.green_seen_frames >= GREEN_CONFIRM_FRAMES

    def update(self, detections):
        now = time.monotonic()
        signals = extract_signals(detections)

        # 处理红灯逻辑
        red_seen = signals["red_seen"]
        green_confirmed = self._update_green_confirm(signals["green_seen"])

        if red_seen and not self.red_light_used and self.state != VehicleState.STOP_RED:
            self.red_light_used = True
            self.red_stop_start_time = now
            self.state = VehicleState.STOP_RED
            self.green_seen_frames = 0
            self.logger.warn("🛑 检测到红灯，触发停车！(仅限本次启动触发一次)")

        if self.state == VehicleState.STOP_RED:
            red_wait_time = now - self.red_stop_start_time
            if green_confirmed:
                self.state = VehicleState.GO
                self.logger.info(f"✅ 连续 {GREEN_CONFIRM_FRAMES} 帧确认绿灯，恢复前进！")
            elif red_wait_time >= RED_LIGHT_MAX_WAIT_SECONDS:
                self.state = VehicleState.GO
                self.logger.info(f"⏰ 红灯等待超时 ({RED_LIGHT_MAX_WAIT_SECONDS}s)，自动恢复前进！")
            
            self._check_state_change()
            return

        # 处理斑马线/停止线逻辑
        crosswalk_seen = signals["crosswalk_seen"]
        
        if self.state == VehicleState.STOP_CROSSWALK:
            if now >= self.crosswalk_stop_until:
                self.state = VehicleState.GO
                self.crosswalk_ignore_until = now + CROSSWALK_RETRIGGER_SUPPRESS_SECONDS
                self.logger.info(f"🚶 斑马线停车完成，恢复前进。进入抑制期 {CROSSWALK_RETRIGGER_SUPPRESS_SECONDS}s")
        elif self.state == VehicleState.GO:
            if crosswalk_seen and now >= self.crosswalk_ignore_until:
                self.state = VehicleState.STOP_CROSSWALK
                self.crosswalk_stop_until = now + CROSSWALK_STOP_SECONDS
                self.logger.warn(f"🛑 检测到斑马线，触发停车 {CROSSWALK_STOP_SECONDS}s！")

        self._check_state_change()

    def _check_state_change(self):
        if self.state != self.last_state:
            self.logger.info(f"🔄 车辆控制状态切换: {self.last_state.value} -> {self.state.value}")
            self.last_state = self.state

# ============================================================
# ROS 2 核心控制节点
# ============================================================

class AutoDriveNode(Node):
    def __init__(self):
        super().__init__('auto_drive_controller')
        self.pub = self.create_publisher(Twist, OUT_TOPIC, 10)
        self.sub = self.create_subscription(Twist, IN_TOPIC, self.cmd_vel_cb, 10)
        
        self.controller = VehicleController(self.get_logger())
        self.hijacked = False
        
        self.get_logger().info(f'节点启动。转发 {IN_TOPIC} -> {OUT_TOPIC}')
        self.get_logger().info('按下 空格键 强制劫持/解除劫持，Ctrl+C 退出程序。')

        # 保存终端设置并开启按键监听线程
        self.settings = termios.tcgetattr(sys.stdin)
        self.key_thread = threading.Thread(target=self.key_listener, daemon=True)
        self.key_thread.start()

        # 开启共享内存 YOLO 读取线程
        self.shm_thread = threading.Thread(target=self.shm_reader, daemon=True)
        self.shm_thread.start()

    def toggle_hijack(self):
        self.hijacked = not self.hijacked
        if self.hijacked:
            self.get_logger().error('⚠️ 手动劫持开启：强制输出零速度！')
            self.publish_zero()
        else:
            self.get_logger().info('✅ 手动劫持关闭：恢复自动驾驶策略。')

    def publish_zero(self):
        stop_msg = Twist()
        self.pub.publish(stop_msg)

    def key_listener(self):
        try:
            tty.setcbreak(sys.stdin.fileno())
            while rclpy.ok():
                char = sys.stdin.read(1)
                if char == HIJACK_KEY:
                    self.toggle_hijack()
                elif char == '\x03': # Ctrl+C
                    break
        finally:
            self.restore_terminal()

    def shm_reader(self):
        self.get_logger().info("等待 YOLO 共享内存就绪...")
        shm = None
        while rclpy.ok() and shm is None:
            try:
                shm = attach_shared_memory_no_unlink(RESULT_SHM_NAME)
            except FileNotFoundError:
                time.sleep(SHM_RETRY_INTERVAL)
        
        self.get_logger().info("🔗 已连接到 YOLO 结果共享内存！")
        last_seq = -1
        last_update = time.monotonic()

        try:
            while rclpy.ok():
                detections, result_seq, infer_fps, camera_seq = read_results(shm)
                now = time.monotonic()

                if detections is not None and result_seq != last_seq:
                    last_seq = result_seq
                    last_update = now
                    self.controller.update(detections)

                # 推理程序重建共享内存后，本进程的旧映射不会失效，但数据永远不再更新。
                # 超时无更新则关闭旧映射，按名字重新连接。
                if now - last_update > SHM_STALE_SECONDS:
                    self.get_logger().warn("YOLO 共享内存长时间无更新，尝试重新连接...")
                    try:
                        shm.close()
                    except Exception:
                        pass
                    shm = None
                    last_seq = -1
                    while rclpy.ok() and shm is None:
                        try:
                            shm = attach_shared_memory_no_unlink(RESULT_SHM_NAME)
                        except FileNotFoundError:
                            time.sleep(SHM_RETRY_INTERVAL)
                    self.get_logger().info("🔗 重新连接到 YOLO 结果共享内存！")
                    last_update = time.monotonic()

                time.sleep(LOOP_SLEEP_SECONDS)
        finally:
            if shm is not None:
                shm.close()

    def cmd_vel_cb(self, msg):
        # 劫持状态 或 自动驾驶逻辑要求停车 时，覆盖为零速度
        if self.hijacked or self.controller.is_stopped():
            self.publish_zero()
        else:
            self.pub.publish(msg)

    def restore_terminal(self):
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, self.settings)

    def destroy_node(self):
        self.restore_terminal()
        super().destroy_node()

def main():
    rclpy.init()
    node = AutoDriveNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()