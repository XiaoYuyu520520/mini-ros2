
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
