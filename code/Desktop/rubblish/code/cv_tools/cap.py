import cv2
import pygame
import time
import os
import sys

# ==========================================
# 可配置参数区
# ==========================================
CAMERA_INDEX = 0                # 摄像头索引 (0 通常是默认的内置摄像头)
WINDOW_WIDTH = 640              # 窗口及摄像头捕获宽度
WINDOW_HEIGHT = 480             # 窗口及摄像头捕获高度
SAVE_INTERVAL = 0.3             # 连续保存图片的间隔时间（秒）
SAVE_DIRECTORY = "snapshots"    # 图片保存的文件夹名称
IMAGE_PREFIX = "capture_"       # 保存图片的文件名前缀
FPS_LIMIT = 60                  # Pygame 窗口的最高刷新率
# ==========================================

def main():
    # 初始化 Pygame
    pygame.init()
    screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
    pygame.display.set_caption("Camera Capture (按空格键 开始/停止 拍照)")
    clock = pygame.time.Clock()

    # 确保保存图片的目录存在
    if not os.path.exists(SAVE_DIRECTORY):
        os.makedirs(SAVE_DIRECTORY)
        print(f"已创建文件夹: {SAVE_DIRECTORY}")

    # 初始化 OpenCV 摄像头
    cap = cv2.VideoCapture(CAMERA_INDEX)
    if not cap.isOpened():
        print("错误：无法打开摄像头！")
        sys.exit()

    # 设置摄像头分辨率
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, WINDOW_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, WINDOW_HEIGHT)

    # 状态变量
    is_recording = False
    last_save_time = 0
    image_counter = 0

    print("系统就绪。请按 [空格键] 开始/停止自动保存图片。")

    running = True
    while running:
        # 1. 处理事件（按键和关闭窗口）
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_SPACE:
                    is_recording = not is_recording
                    state_msg = "开始" if is_recording else "停止"
                    print(f"[{state_msg}] 自动保存图片...")

        # 2. 从 OpenCV 读取一帧
        ret, frame = cap.read()
        if not ret:
            print("警告：无法获取画面")
            continue

        # 3. 将 OpenCV 的 BGR 图像转换为 Pygame 可用的 RGB 图像
        # OpenCV 默认颜色空间是 BGR，而 Pygame 需要 RGB
        frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        
        # 将 numpy 数组转换为 Pygame Surface
        # frame_rgb.shape[1] 是宽度, shape[0] 是高度
        surface = pygame.image.frombuffer(
            frame_rgb.tobytes(), 
            (frame_rgb.shape[1], frame_rgb.shape[0]), 
            'RGB'
        )

        # 4. 在屏幕上绘制图像
        screen.blit(surface, (0, 0))

        # 5. 如果处于录制状态，检查是否到达保存时间
        if is_recording:
            current_time = time.time()
            if current_time - last_save_time >= SAVE_INTERVAL:
                # 构造文件名并保存
                filename = os.path.join(SAVE_DIRECTORY, f"{IMAGE_PREFIX}{image_counter:04d}.jpg")
                # 使用原始的 frame (BGR) 来保存，因为 cv2.imwrite 需要 BGR 格式
                cv2.imwrite(filename, frame)
                print(f"已保存: {filename}")
                
                last_save_time = current_time
                image_counter += 1

                # 可选：在屏幕上闪烁一个红点提示正在录制
                pygame.draw.circle(screen, (255, 0, 0), (WINDOW_WIDTH - 30, 30), 10)

        # 6. 更新显示并控制帧率
        pygame.display.flip()
        clock.tick(FPS_LIMIT)

    # 释放资源
    cap.release()
    pygame.quit()
    print("程序已退出。")

if __name__ == "__main__":
    main()
