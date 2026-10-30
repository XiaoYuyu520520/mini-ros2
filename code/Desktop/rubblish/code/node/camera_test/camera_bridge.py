import os
import subprocess
import time
import numpy as np
import cv2
from multiprocessing import shared_memory
from multiprocessing.resource_tracker import unregister  # 修复内存清理警告的关键
import posix_ipc
from cpu_utils import ProcessMonitor

# ================= 配置参数 =================
RECOMPILE_CPP = True          # 建议先设为 True，强制应用最新的 C++ 逻辑
SHM_NAME = "camera_shm"       # Python 端使用的名称 (底层会自动加 /)
CPP_SHM_NAME = "/" + SHM_NAME # C++ 端 POSIX 标准严格要求带 /
DEV_VIDEO = "/dev/video0"
WIDTH = 1280
HEIGHT = 720
CHANNELS = 3
HEADER_SIZE = 64
SEM_NAME = "/camera_sem"
# ===========================================

CPP_SOURCE = r'''
#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include <opencv2/opencv.hpp>
#include <csignal>
#include <cstring>

struct ShmHeader {
    uint64_t frame_count;
    int32_t width;
    int32_t height;
};

bool keep_running = true;
void signal_handler(int) { keep_running = false; }

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string shm_name = argv[1];
    int width = std::stoi(argv[2]);
    int height = std::stoi(argv[3]);
    std::string sem_name = argv[4];
    size_t img_size = width * height * 3;
    size_t total_size = img_size + 64;

    int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        std::cerr << "[C++] 错误: shm_open 失败!" << std::endl;
        return -1;
    }
    if (ftruncate(fd, total_size) == -1) {
        std::cerr << "[C++] 错误: ftruncate 失败!" << std::endl;
        return -1;
    }
    void* ptr = mmap(0, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "[C++] 错误: mmap 失败!" << std::endl;
        return -1;
    }

    sem_t* sem = sem_open(sem_name.c_str(), O_CREAT, 0666, 0);
    if (sem == SEM_FAILED) {
        std::cerr << "[C++] 错误: sem_open 失败!" << std::endl;
        return -1;
    }

    ShmHeader* header = (ShmHeader*)ptr;
    uint8_t* data_ptr = (uint8_t*)ptr + 64;

    header->width = width;
    header->height = height;
    header->frame_count = 0;

    cv::VideoCapture cap(0, cv::CAP_V4L2);
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap.set(cv::CAP_PROP_FPS, 60);

    if (!cap.isOpened()) {
        std::cerr << "[C++] 严重错误: 无法打开摄像头!" << std::endl;
        return -1;
    }

    std::cout << "[C++] 开始采集: " << width << "x" << height << " @ 60FPS" << std::endl;

    cv::Mat temp;
    while (keep_running) {
        if (!cap.read(temp)) continue;

        if (temp.isContinuous()) {
            std::memcpy(data_ptr, temp.data, img_size);
        } else {
            for (int i = 0; i < temp.rows; ++i) {
                std::memcpy(data_ptr + i * temp.cols * 3, temp.ptr(i), temp.cols * 3);
            }
        }

        header->frame_count++;

        int sem_val;
        sem_getvalue(sem, &sem_val);
        if (sem_val < 1) {
            sem_post(sem);
        }
    }

    std::cout << "[C++] 正在退出并清理内存..." << std::endl;
    munmap(ptr, total_size);
    close(fd);
    shm_unlink(shm_name.c_str());

    sem_close(sem);
    sem_unlink(sem_name.c_str());
    return 0;
}
'''

def compile_and_run():
    with open("shm_producer.cpp", "w") as f:
        f.write(CPP_SOURCE)

    if RECOMPILE_CPP or not os.path.exists("./shm_producer"):
        print("正在编译 C++ 生产者...")
        cmd = ["g++", "-O3", "shm_producer.cpp", "-o", "shm_producer",
               "`pkg-config", "--cflags", "--libs", "opencv4`", "-lrt", "-lpthread"]
        subprocess.run(" ".join(cmd), shell=True, check=True)

    producer_proc = subprocess.Popen(["./shm_producer", CPP_SHM_NAME, str(WIDTH), str(HEIGHT), SEM_NAME])

    print("[Python] 正在等待 C++ 生产者就绪...")
    shm = None
    
    # --- 修复 1：动态轮询等待替代 time.sleep(2)，防止时序崩溃 ---
    for _ in range(50):  # 最多等待 5 秒
        if producer_proc.poll() is not None:
            print("\n[Python] 错误：C++ 生产者异常退出，请检查上方报错输出。")
            return
        try:
            shm = shared_memory.SharedMemory(name=SHM_NAME)
            break
        except FileNotFoundError:
            time.sleep(0.1)

    if shm is None:
        print("\n[Python] 错误：等待超时，无法连接到共享内存。")
        return

    # --- 修复 2：注销 Python 的自动清理，解除 resource_tracker 警告 ---
    unregister(shm._name, 'shared_memory')

    monitor = ProcessMonitor(child_pid=producer_proc.pid)

    try:
        frame_buffer = np.ndarray((HEIGHT, WIDTH, CHANNELS), dtype=np.uint8, buffer=shm.buf, offset=HEADER_SIZE)

        # --- 修复 3：添加 O_CREAT 标志，保证信号量在跨进程时的鲁棒性 ---
        sem = posix_ipc.Semaphore(SEM_NAME, flags=posix_ipc.O_CREAT)

        print("[Python] 消费者已就绪，正在纯后端模式下读取画面...")
        fps_counter = 0
        start_time = time.time()

        while True:
            sem.acquire()

            fps_counter += 1

            if time.time() - start_time >= 1.0:
                center_pixel = frame_buffer[HEIGHT//2, WIDTH//2]
                cpu_stats = monitor.get_stats()

                print(f"\r[监控] {fps_counter} FPS | 中心像素: {center_pixel} | "
                      f"CPU - 系统: {cpu_stats['system_cpu']}%  "
                      f"Python: {cpu_stats['python_cpu']}%  "
                      f"C++: {cpu_stats['cpp_cpu']}%", end="")

                fps_counter = 0
                start_time = time.time()

    except KeyboardInterrupt:
        print("\n检测到 Ctrl+C，准备退出...")
    except posix_ipc.ExistentialError:
        print(f"\n[Python] 找不到信号量 '{SEM_NAME}'。")
    finally:
        print("正在清理资源...")
        producer_proc.terminate()
        try:
            shm.close()
            sem.close()
        except:
            pass

if __name__ == "__main__":
    compile_and_run()