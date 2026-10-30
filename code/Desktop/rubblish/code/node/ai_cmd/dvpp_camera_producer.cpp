#define ENABLE_DVPP_INTERFACE

#include <acl/acl.h>
#include <acl/ops/acl_dvpp.h>

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

// ================= 配置参数 =================
static const char* DEV_VIDEO = "auto";
static const char* SHM_NAME = "/dvpp_cam_nv12";

constexpr int DEVICE_ID = 0;

constexpr uint32_t CAM_WIDTH = 640;
constexpr uint32_t CAM_HEIGHT = 480;
constexpr uint32_t CAM_FPS = 10;

constexpr uint32_t MODEL_WIDTH = 640;
constexpr uint32_t MODEL_HEIGHT = 640;

constexpr uint32_t SHM_HEADER_SIZE = 4096;
constexpr uint32_t MODEL_NV12_SIZE = MODEL_WIDTH * MODEL_HEIGHT * 3 / 2;

constexpr uint32_t V4L2_BUFFER_COUNT = 4;
constexpr uint32_t MAX_JPEG_SIZE = 2 * 1024 * 1024;
constexpr uint32_t MAX_RAW_FRAME = 1920 * 1080 * 2;

// 只做最低保护，不再用 50KB 这种分辨率相关阈值
constexpr size_t MIN_VALID_JPEG_SIZE = 1024;
// ===========================================

#pragma pack(push, 1)
struct ShmHeader {
    uint64_t seq;
    uint64_t timestamp_ns;
    double camera_fps;
    double pipeline_ms;

    uint32_t width;
    uint32_t height;
    uint32_t orig_width;
    uint32_t orig_height;
    uint32_t bytesused;
    uint32_t pad_left;
    uint32_t pad_top;
    float scale;
};
#pragma pack(pop)

static_assert(sizeof(ShmHeader) == 64, "ShmHeader must be 64 bytes");

struct V4L2Buffer {
    void* start = nullptr;
    size_t length = 0;
};

static volatile bool keep_running = true;

static void signal_handler(int) {
    keep_running = false;
}

static int xioctl(int fd, unsigned long req, void* arg) {
    int ret;
    do {
        ret = ioctl(fd, req, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

static uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a)
        | (static_cast<uint32_t>(b) << 8)
        | (static_cast<uint32_t>(c) << 16)
        | (static_cast<uint32_t>(d) << 24);
}

static std::string fourcc_to_str(uint32_t v) {
    std::string s;
    s += static_cast<char>(v & 0xff);
    s += static_cast<char>((v >> 8) & 0xff);
    s += static_cast<char>((v >> 16) & 0xff);
    s += static_cast<char>((v >> 24) & 0xff);
    return s;
}

static uint32_t align_up(uint32_t x, uint32_t align) {
    return (x + align - 1) / align * align;
}

static uint64_t timeval_to_ns(const timeval& tv) {
    return static_cast<uint64_t>(tv.tv_sec) * 1000000000ULL
         + static_cast<uint64_t>(tv.tv_usec) * 1000ULL;
}

static bool check_acl(aclError ret, const char* name) {
    if (ret != ACL_SUCCESS) {
        std::cerr << "[ERROR] " << name << " failed, ret=" << ret << std::endl;
        return false;
    }
    return true;
}

static bool check_jpeg_integrity(const void* data, size_t size, bool& out_has_eoi) {
    out_has_eoi = false;
    if (!data || size < 4) return false;

    const auto* bytes = static_cast<const uint8_t*>(data);

    // SOI: FF D8
    if (bytes[0] != 0xFF || bytes[1] != 0xD8) return false;

    // EOI: FF D9。部分 UVC MJPEG 可能没有标准 EOI，但 DVPP 有时仍能解码。
    out_has_eoi = (bytes[size - 2] == 0xFF && bytes[size - 1] == 0xD9);
    return true;
}

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "  --device PATH    Camera device (default: auto-detect)\n"
              << "  --shm NAME       Shared memory name (default: " << SHM_NAME << ")\n"
              << "  Fixed capture request: " << CAM_WIDTH << "x" << CAM_HEIGHT
              << " @ " << CAM_FPS << "fps\n"
              << "  --help           Show this help\n";
}

static bool parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--device" && i + 1 < argc) {
            DEV_VIDEO = argv[++i];
        } else if (arg == "--shm" && i + 1 < argc) {
            SHM_NAME = argv[++i];
        } else {
            std::cerr << "[ERROR] Unknown option: " << arg << std::endl;
            return false;
        }
    }
    return true;
}

static bool is_video_device(const std::string& path) {
    int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) return false;

    v4l2_capability cap {};
    bool ok = (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
           && (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE);

    close(fd);
    return ok;
}

static std::string detect_camera() {
    if (DEV_VIDEO && strcmp(DEV_VIDEO, "") != 0 && strcmp(DEV_VIDEO, "auto") != 0) {
        if (is_video_device(DEV_VIDEO)) {
            std::cout << "[CAM] Using specified device: " << DEV_VIDEO << std::endl;
            return DEV_VIDEO;
        }

        std::cerr << "[WARN] Specified device " << DEV_VIDEO
                  << " not found, auto-detecting..." << std::endl;
    }

    for (int i = 0; i < 8; ++i) {
        std::ostringstream path;
        path << "/dev/video" << i;

        if (is_video_device(path.str())) {
            std::cout << "[CAM] Detected camera at " << path.str() << std::endl;
            DEV_VIDEO = strdup(path.str().c_str());
            return DEV_VIDEO;
        }
    }

    return "";
}

static void enumerate_formats(int fd) {
    std::cout << "[V4L2] Available formats:" << std::endl;

    v4l2_fmtdesc fmt {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.index = 0;

    while (xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        std::cout << "  " << fourcc_to_str(fmt.pixelformat)
                  << " - " << fmt.description << std::endl;

        v4l2_frmsizeenum fsize {};
        fsize.pixel_format = fmt.pixelformat;
        fsize.index = 0;

        while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsize) == 0) {
            if (fsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                v4l2_frmivalenum fival {};
                fival.pixel_format = fmt.pixelformat;
                fival.width = fsize.discrete.width;
                fival.height = fsize.discrete.height;
                fival.index = 0;

                std::string fps_list;

                while (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fival) == 0) {
                    if (fival.type == V4L2_FRMIVAL_TYPE_DISCRETE &&
                        fival.discrete.numerator != 0) {
                        if (!fps_list.empty()) fps_list += "/";

                        double fps = static_cast<double>(fival.discrete.denominator) /
                                     static_cast<double>(fival.discrete.numerator);

                        std::ostringstream ss;
                        ss << fps;
                        fps_list += ss.str();
                    }

                    fival.index++;
                }

                std::cout << "    " << fsize.discrete.width << "x"
                          << fsize.discrete.height << " [" << fps_list << "fps]"
                          << std::endl;
            } else if (fsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
                std::cout << "    continuous: "
                          << fsize.stepwise.min_width << "x" << fsize.stepwise.min_height
                          << " - "
                          << fsize.stepwise.max_width << "x" << fsize.stepwise.max_height
                          << std::endl;
            }

            fsize.index++;
        }

        fmt.index++;
    }
}

static void print_stream_fps(int fd, const char* tag) {
    v4l2_streamparm parm {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(fd, VIDIOC_G_PARM, &parm) < 0) {
        std::cerr << "[WARN] " << tag << " VIDIOC_G_PARM failed: "
                  << strerror(errno) << std::endl;
        return;
    }

    auto num = parm.parm.capture.timeperframe.numerator;
    auto den = parm.parm.capture.timeperframe.denominator;

    if (num != 0 && den != 0) {
        double fps = static_cast<double>(den) / static_cast<double>(num);
        std::cout << "[V4L2] " << tag << " fps: " << fps
                  << " interval=" << num << "/" << den << std::endl;
    } else {
        std::cout << "[V4L2] " << tag << " fps: unknown"
                  << " interval=" << num << "/" << den << std::endl;
    }
}

static bool setup_v4l2(int& fd,
                       std::vector<V4L2Buffer>& buffers,
                       uint32_t& out_width,
                       uint32_t& out_height,
                       uint32_t& out_pixelformat,
                       uint32_t& out_sizeimage) {
    fd = open(DEV_VIDEO, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "[ERROR] open " << DEV_VIDEO << " failed: "
                  << strerror(errno) << std::endl;
        return false;
    }

    v4l2_capability cap {};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "[ERROR] VIDIOC_QUERYCAP failed: "
                  << strerror(errno) << std::endl;
        close(fd);
        fd = -1;
        return false;
    }

    std::cout << "[V4L2] driver: " << cap.driver << std::endl;
    std::cout << "[V4L2] card  : " << cap.card << std::endl;

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        std::cerr << "[ERROR] Not a video capture device" << std::endl;
        close(fd);
        fd = -1;
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        std::cerr << "[ERROR] Device does not support streaming I/O" << std::endl;
        close(fd);
        fd = -1;
        return false;
    }

    // 调试时建议打开，确认摄像头到底支持哪些 FPS。
    enumerate_formats(fd);

    const uint32_t formats_to_try[] = {
        fourcc('M', 'J', 'P', 'G'),
        fourcc('Y', 'U', 'Y', 'V'),
        fourcc('Y', 'U', 'Y', '2'),
    };

    bool format_set = false;

    for (auto pix_fmt : formats_to_try) {
        v4l2_format fmt {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = CAM_WIDTH;
        fmt.fmt.pix.height = CAM_HEIGHT;
        fmt.fmt.pix.pixelformat = pix_fmt;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;

        if (xioctl(fd, VIDIOC_S_FMT, &fmt) == 0 &&
            fmt.fmt.pix.pixelformat == pix_fmt) {
            out_width = fmt.fmt.pix.width;
            out_height = fmt.fmt.pix.height;
            out_pixelformat = fmt.fmt.pix.pixelformat;
            out_sizeimage = fmt.fmt.pix.sizeimage;
            format_set = true;

            std::cout << "[V4L2] Set format: "
                      << out_width << "x" << out_height
                      << " " << fourcc_to_str(out_pixelformat)
                      << std::endl;

            std::cout << "[V4L2] bytesperline=" << fmt.fmt.pix.bytesperline
                      << " sizeimage=" << fmt.fmt.pix.sizeimage
                      << " field=" << fmt.fmt.pix.field
                      << " colorspace=" << fmt.fmt.pix.colorspace
                      << std::endl;

            break;
        }
    }

    if (!format_set) {
        std::cerr << "[ERROR] Could not set any known format at "
                  << CAM_WIDTH << "x" << CAM_HEIGHT << std::endl;
        enumerate_formats(fd);
        close(fd);
        fd = -1;
        return false;
    }

    print_stream_fps(fd, "before S_PARM");

    v4l2_streamparm parm {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = CAM_FPS;

    if (xioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
        std::cerr << "[WARN] VIDIOC_S_PARM failed: "
                  << strerror(errno) << std::endl;
    } else {
        std::cout << "[V4L2] Requested fps: " << CAM_FPS << std::endl;

        auto num = parm.parm.capture.timeperframe.numerator;
        auto den = parm.parm.capture.timeperframe.denominator;

        if (num != 0 && den != 0) {
            double actual_fps = static_cast<double>(den) / static_cast<double>(num);
            std::cout << "[V4L2] S_PARM returned fps: " << actual_fps
                      << " interval=" << num << "/" << den << std::endl;
        }
    }

    print_stream_fps(fd, "after S_PARM");

    v4l2_requestbuffers req {};
    req.count = V4L2_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[ERROR] VIDIOC_REQBUFS failed: "
                  << strerror(errno) << std::endl;
        close(fd);
        fd = -1;
        return false;
    }

    if (req.count < 2) {
        std::cerr << "[ERROR] insufficient V4L2 buffers" << std::endl;
        close(fd);
        fd = -1;
        return false;
    }

    buffers.resize(req.count);

    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[ERROR] VIDIOC_QUERYBUF failed: "
                      << strerror(errno) << std::endl;
            close(fd);
            fd = -1;
            return false;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(nullptr,
                                buf.length,
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED,
                                fd,
                                buf.m.offset);

        if (buffers[i].start == MAP_FAILED) {
            std::cerr << "[ERROR] mmap V4L2 buffer failed: "
                      << strerror(errno) << std::endl;
            close(fd);
            fd = -1;
            return false;
        }
    }

    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[ERROR] VIDIOC_QBUF failed: "
                      << strerror(errno) << std::endl;
            close(fd);
            fd = -1;
            return false;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[ERROR] VIDIOC_STREAMON failed: "
                  << strerror(errno) << std::endl;
        close(fd);
        fd = -1;
        return false;
    }

    std::cout << "[V4L2] streaming started, " << req.count << " buffers"
              << std::endl;

    return true;
}

static bool setup_shm(int& shm_fd,
                      void*& shm_ptr,
                      ShmHeader*& header,
                      uint8_t*& data_ptr) {
    shm_unlink(SHM_NAME);

    const size_t total_size = SHM_HEADER_SIZE + MODEL_NV12_SIZE;

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        std::cerr << "[ERROR] shm_open failed: "
                  << strerror(errno) << std::endl;
        return false;
    }

    if (ftruncate(shm_fd, total_size) < 0) {
        std::cerr << "[ERROR] ftruncate failed: "
                  << strerror(errno) << std::endl;
        close(shm_fd);
        shm_fd = -1;
        return false;
    }

    shm_ptr = mmap(nullptr,
                   total_size,
                   PROT_READ | PROT_WRITE,
                   MAP_SHARED,
                   shm_fd,
                   0);

    if (shm_ptr == MAP_FAILED) {
        std::cerr << "[ERROR] mmap shm failed: "
                  << strerror(errno) << std::endl;
        close(shm_fd);
        shm_fd = -1;
        return false;
    }

    std::memset(shm_ptr, 0, total_size);

    header = reinterpret_cast<ShmHeader*>(shm_ptr);
    data_ptr = reinterpret_cast<uint8_t*>(shm_ptr) + SHM_HEADER_SIZE;

    header->width = MODEL_WIDTH;
    header->height = MODEL_HEIGHT;
    header->bytesused = MODEL_NV12_SIZE;

    std::cout << "[SHM] created: " << SHM_NAME
              << ", size=" << total_size << std::endl;

    return true;
}

static bool setup_dvpp(aclrtContext& context,
                       aclrtStream& stream,
                       acldvppChannelDesc*& channel_desc,
                       void*& jpeg_dev,
                       void*& decoded_dev,
                       void*& model_nv12_dev,
                       acldvppPicDesc*& decoded_desc,
                       acldvppPicDesc*& model_desc,
                       acldvppRoiConfig*& crop_area,
                       acldvppRoiConfig*& paste_area,
                       uint32_t cam_w,
                       uint32_t cam_h,
                       bool is_mjpeg) {
    aclError ret;

    ret = aclrtCreateContext(&context, DEVICE_ID);
    if (!check_acl(ret, "aclrtCreateContext")) return false;

    ret = aclrtCreateStream(&stream);
    if (!check_acl(ret, "aclrtCreateStream")) return false;

    channel_desc = acldvppCreateChannelDesc();
    if (!channel_desc) {
        std::cerr << "[ERROR] acldvppCreateChannelDesc failed" << std::endl;
        return false;
    }

    ret = acldvppCreateChannel(channel_desc);
    if (!check_acl(ret, "acldvppCreateChannel")) return false;

    if (is_mjpeg) {
        ret = acldvppMalloc(&jpeg_dev, MAX_JPEG_SIZE);
        if (!check_acl(ret, "acldvppMalloc jpeg_dev")) return false;
    }

    const uint32_t decoded_ws = align_up(cam_w, 128);
    const uint32_t decoded_hs = align_up(cam_h, 16);
    const uint32_t decoded_size = decoded_ws * decoded_hs * 3 / 2;

    ret = acldvppMalloc(&decoded_dev, decoded_size);
    if (!check_acl(ret, "acldvppMalloc decoded_dev")) return false;

    ret = acldvppMalloc(&model_nv12_dev, MODEL_NV12_SIZE);
    if (!check_acl(ret, "acldvppMalloc model_nv12_dev")) return false;

    decoded_desc = acldvppCreatePicDesc();
    model_desc = acldvppCreatePicDesc();

    if (!decoded_desc || !model_desc) {
        std::cerr << "[ERROR] acldvppCreatePicDesc failed" << std::endl;
        return false;
    }

    acldvppSetPicDescData(decoded_desc, decoded_dev);
    acldvppSetPicDescSize(decoded_desc, decoded_size);
    acldvppSetPicDescFormat(decoded_desc, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
    acldvppSetPicDescWidth(decoded_desc, cam_w);
    acldvppSetPicDescHeight(decoded_desc, cam_h);
    acldvppSetPicDescWidthStride(decoded_desc, decoded_ws);
    acldvppSetPicDescHeightStride(decoded_desc, decoded_hs);

    acldvppSetPicDescData(model_desc, model_nv12_dev);
    acldvppSetPicDescSize(model_desc, MODEL_NV12_SIZE);
    acldvppSetPicDescFormat(model_desc, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
    acldvppSetPicDescWidth(model_desc, MODEL_WIDTH);
    acldvppSetPicDescHeight(model_desc, MODEL_HEIGHT);
    acldvppSetPicDescWidthStride(model_desc, MODEL_WIDTH);
    acldvppSetPicDescHeightStride(model_desc, MODEL_HEIGHT);

    crop_area = acldvppCreateRoiConfig(0, cam_w - 1, 0, cam_h - 1);

    const float scale = std::min(static_cast<float>(MODEL_WIDTH) / cam_w,
                                 static_cast<float>(MODEL_HEIGHT) / cam_h);

    uint32_t scaled_w = static_cast<uint32_t>(cam_w * scale);
    uint32_t scaled_h = static_cast<uint32_t>(cam_h * scale);

    if (scaled_w == 0) scaled_w = 1;
    if (scaled_h == 0) scaled_h = 1;

    const uint32_t pad_left = (MODEL_WIDTH - scaled_w) / 2;
    const uint32_t pad_top = (MODEL_HEIGHT - scaled_h) / 2;

    paste_area = acldvppCreateRoiConfig(pad_left,
                                        pad_left + scaled_w - 1,
                                        pad_top,
                                        pad_top + scaled_h - 1);

    if (!crop_area || !paste_area) {
        std::cerr << "[ERROR] acldvppCreateRoiConfig failed" << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (!parse_args(argc, argv)) {
        print_usage(argv[0]);
        return 1;
    }

    std::string cam_path = detect_camera();
    if (cam_path.empty()) {
        std::cerr << "[ERROR] No camera device found" << std::endl;
        return 1;
    }

    int v4l2_fd = -1;
    int shm_fd = -1;

    std::vector<V4L2Buffer> v4l2_buffers;

    void* shm_ptr = nullptr;
    ShmHeader* shm_header = nullptr;
    uint8_t* shm_data = nullptr;

    aclrtContext context = nullptr;
    aclrtStream stream = nullptr;
    acldvppChannelDesc* channel_desc = nullptr;

    void* jpeg_dev = nullptr;
    void* decoded_dev = nullptr;
    void* model_nv12_dev = nullptr;
    void* raw_frame_dev = nullptr;

    acldvppPicDesc* decoded_desc = nullptr;
    acldvppPicDesc* model_desc = nullptr;

    acldvppRoiConfig* crop_area = nullptr;
    acldvppRoiConfig* paste_area = nullptr;

    bool acl_inited = false;
    bool device_set = false;
    bool stream_on = false;
    bool cleanup_needed = false;

    auto cleanup = [&]() {
        if (!cleanup_needed) return;

        if (v4l2_fd >= 0 && stream_on) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            xioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
            stream_on = false;
        }

        for (auto& b : v4l2_buffers) {
            if (b.start && b.start != MAP_FAILED) {
                munmap(b.start, b.length);
                b.start = nullptr;
            }
        }

        if (v4l2_fd >= 0) {
            close(v4l2_fd);
            v4l2_fd = -1;
        }

        if (decoded_desc) {
            acldvppDestroyPicDesc(decoded_desc);
            decoded_desc = nullptr;
        }

        if (model_desc) {
            acldvppDestroyPicDesc(model_desc);
            model_desc = nullptr;
        }

        if (crop_area) {
            acldvppDestroyRoiConfig(crop_area);
            crop_area = nullptr;
        }

        if (paste_area) {
            acldvppDestroyRoiConfig(paste_area);
            paste_area = nullptr;
        }

        if (jpeg_dev) {
            acldvppFree(jpeg_dev);
            jpeg_dev = nullptr;
        }

        if (decoded_dev) {
            acldvppFree(decoded_dev);
            decoded_dev = nullptr;
        }

        if (model_nv12_dev) {
            acldvppFree(model_nv12_dev);
            model_nv12_dev = nullptr;
        }

        if (raw_frame_dev) {
            acldvppFree(raw_frame_dev);
            raw_frame_dev = nullptr;
        }

        if (channel_desc) {
            acldvppDestroyChannel(channel_desc);
            acldvppDestroyChannelDesc(channel_desc);
            channel_desc = nullptr;
        }

        if (stream) {
            aclrtDestroyStream(stream);
            stream = nullptr;
        }

        if (context) {
            aclrtDestroyContext(context);
            context = nullptr;
        }

        if (device_set) {
            aclrtResetDevice(DEVICE_ID);
            device_set = false;
        }

        if (acl_inited) {
            aclFinalize();
            acl_inited = false;
        }

        if (shm_ptr && shm_ptr != MAP_FAILED) {
            munmap(shm_ptr, SHM_HEADER_SIZE + MODEL_NV12_SIZE);
            shm_ptr = nullptr;
        }

        if (shm_fd >= 0) {
            close(shm_fd);
            shm_fd = -1;
        }

        shm_unlink(SHM_NAME);
    };

    cleanup_needed = true;

    if (!setup_shm(shm_fd, shm_ptr, shm_header, shm_data)) {
        cleanup();
        return 1;
    }

    uint32_t cam_w = 0;
    uint32_t cam_h = 0;
    uint32_t cam_pixfmt = 0;
    uint32_t cam_sizeimage = 0;

    if (!setup_v4l2(v4l2_fd,
                    v4l2_buffers,
                    cam_w,
                    cam_h,
                    cam_pixfmt,
                    cam_sizeimage)) {
        cleanup();
        return 1;
    }

    stream_on = true;

    const bool is_mjpeg = (cam_pixfmt == fourcc('M', 'J', 'P', 'G'));

    float scale = std::min(static_cast<float>(MODEL_WIDTH) / cam_w,
                           static_cast<float>(MODEL_HEIGHT) / cam_h);

    uint32_t scaled_w = static_cast<uint32_t>(cam_w * scale);
    uint32_t scaled_h = static_cast<uint32_t>(cam_h * scale);

    if (scaled_w == 0) scaled_w = 1;
    if (scaled_h == 0) scaled_h = 1;

    uint32_t pad_left = (MODEL_WIDTH - scaled_w) / 2;
    uint32_t pad_top = (MODEL_HEIGHT - scaled_h) / 2;

    std::cout << "[INFO] Camera " << cam_w << "x" << cam_h
              << " " << fourcc_to_str(cam_pixfmt)
              << " -> model " << MODEL_WIDTH << "x" << MODEL_HEIGHT
              << " letterbox pad=(" << pad_left << "," << pad_top << ")"
              << " scale=" << scale << std::endl;

    aclError ret = aclInit(nullptr);
    if (!check_acl(ret, "aclInit")) {
        cleanup();
        return 1;
    }
    acl_inited = true;
    std::cout << "[INFO] acl init success" << std::endl;

    ret = aclrtSetDevice(DEVICE_ID);
    if (!check_acl(ret, "aclrtSetDevice")) {
        cleanup();
        return 1;
    }
    device_set = true;
    std::cout << "[INFO] open device " << DEVICE_ID << " success" << std::endl;

    if (!setup_dvpp(context,
                    stream,
                    channel_desc,
                    jpeg_dev,
                    decoded_dev,
                    model_nv12_dev,
                    decoded_desc,
                    model_desc,
                    crop_area,
                    paste_area,
                    cam_w,
                    cam_h,
                    is_mjpeg)) {
        cleanup();
        return 1;
    }

    uint32_t raw_frame_size = 0;

    if (!is_mjpeg) {
        raw_frame_size = std::max<uint32_t>(cam_sizeimage, cam_w * cam_h * 2);

        if (raw_frame_size == 0 || raw_frame_size > MAX_RAW_FRAME) {
            raw_frame_size = cam_w * cam_h * 2;
        }

        ret = acldvppMalloc(&raw_frame_dev, raw_frame_size);
        if (!check_acl(ret, "acldvppMalloc raw_frame_dev")) {
            cleanup();
            return 1;
        }
    }

    std::cout << "[DVPP] " << (is_mjpeg ? "JPEGD" : "VPC direct")
              << " + VPC ready" << std::endl;

    std::cout << "[RUN] producer started. Target publish fps="
              << CAM_FPS << ". Press Ctrl+C to stop." << std::endl;

    uint64_t seq = 0;

    const uint32_t y_size = MODEL_WIDTH * MODEL_HEIGHT;
    const uint32_t uv_size = y_size / 2;

    const auto publish_interval =
        std::chrono::microseconds(1000000 / CAM_FPS);

    auto last_publish_time =
        std::chrono::steady_clock::now() - publish_interval;

    bool has_last_publish_time = false;
    double publish_fps = 0.0;

    while (keep_running) {
        pollfd pfd {};
        pfd.fd = v4l2_fd;
        pfd.events = POLLIN;

        int pret = poll(&pfd, 1, 1000);
        if (pret < 0) {
            if (errno == EINTR) continue;

            std::cerr << "[ERROR] poll failed: "
                      << strerror(errno) << std::endl;
            break;
        }

        if (pret == 0) {
            std::cerr << "[WARN] poll timeout" << std::endl;
            continue;
        }

        v4l2_buffer buf {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(v4l2_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;

            std::cerr << "[ERROR] VIDIOC_DQBUF failed: "
                      << strerror(errno) << std::endl;
            break;
        }

        // ================= 软件限帧核心逻辑 =================
        // 摄像头可以 30/60/120fps 吐帧，但 producer 只处理并发布 CAM_FPS。
        auto now = std::chrono::steady_clock::now();

        if (now - last_publish_time < publish_interval) {
            if (xioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "[ERROR] VIDIOC_QBUF after drop failed: "
                          << strerror(errno) << std::endl;
                break;
            }
            continue;
        }

        if (has_last_publish_time) {
            double dt = std::chrono::duration<double>(now - last_publish_time).count();
            if (dt > 0.000001) {
                publish_fps = 1.0 / dt;
            }
        } else {
            publish_fps = static_cast<double>(CAM_FPS);
            has_last_publish_time = true;
        }

        last_publish_time = now;
        // =====================================================

        auto t0 = std::chrono::steady_clock::now();

        // 1. Fill model NV12 background: Y=114, UV=128
        ret = aclrtMemsetAsync(model_nv12_dev,
                               y_size,
                               114,
                               y_size,
                               stream);

        if (ret != ACL_SUCCESS) {
            std::cerr << "[ERROR] aclrtMemsetAsync Y failed, ret="
                      << ret << std::endl;
            xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
            continue;
        }

        ret = aclrtMemsetAsync(static_cast<uint8_t*>(model_nv12_dev) + y_size,
                               uv_size,
                               128,
                               uv_size,
                               stream);

        if (ret != ACL_SUCCESS) {
            std::cerr << "[ERROR] aclrtMemsetAsync UV failed, ret="
                      << ret << std::endl;
            xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
            continue;
        }

        // 2. H2D + DVPP
        if (is_mjpeg) {
            if (buf.bytesused > MAX_JPEG_SIZE) {
                std::cerr << "[WARN] JPEG too large: "
                          << buf.bytesused << std::endl;
                xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                continue;
            }

            if (buf.bytesused < MIN_VALID_JPEG_SIZE) {
                std::cerr << "[WARN] JPEG suspiciously small, size="
                          << buf.bytesused << std::endl;
                xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                continue;
            }

            bool has_eoi = false;

            if (!check_jpeg_integrity(v4l2_buffers[buf.index].start,
                                      buf.bytesused,
                                      has_eoi)) {
                std::cerr << "[WARN] JPEG missing SOI marker, size="
                          << buf.bytesused
                          << " seq=" << seq
                          << std::endl;
                xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                continue;
            }

            static bool eoi_warned = false;
            if (!has_eoi && !eoi_warned) {
                eoi_warned = true;
                std::cerr << "[WARN] JPEG missing EOI marker, still trying decode, size="
                          << buf.bytesused << std::endl;
            }

            ret = aclrtMemcpyAsync(jpeg_dev,
                                   MAX_JPEG_SIZE,
                                   v4l2_buffers[buf.index].start,
                                   buf.bytesused,
                                   ACL_MEMCPY_HOST_TO_DEVICE,
                                   stream);

            if (ret != ACL_SUCCESS) {
                std::cerr << "[ERROR] aclrtMemcpyAsync JPEG H2D failed, ret="
                          << ret << std::endl;
                xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                continue;
            }

            ret = acldvppJpegDecodeAsync(channel_desc,
                                         jpeg_dev,
                                         buf.bytesused,
                                         decoded_desc,
                                         stream);

            if (ret != ACL_SUCCESS) {
                const auto* raw =
                    static_cast<const uint8_t*>(v4l2_buffers[buf.index].start);

                std::cerr << "[ERROR] acldvppJpegDecodeAsync failed, ret="
                          << ret
                          << " size=" << buf.bytesused
                          << " first_bytes=" << std::hex
                          << (raw && buf.bytesused >= 2 ? static_cast<int>(raw[0]) : -1)
                          << " "
                          << (raw && buf.bytesused >= 2 ? static_cast<int>(raw[1]) : -1)
                          << std::dec
                          << " seq=" << seq
                          << std::endl;

                xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                continue;
            }

            ret = acldvppVpcCropAndPasteAsync(channel_desc,
                                              decoded_desc,
                                              model_desc,
                                              crop_area,
                                              paste_area,
                                              stream);

            if (ret != ACL_SUCCESS) {
                std::cerr << "[ERROR] acldvppVpcCropAndPasteAsync failed, ret="
                          << ret << std::endl;
                xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                continue;
            }
        } else {
            if (buf.bytesused > raw_frame_size) {
                std::cerr << "[WARN] Raw frame too large: bytesused="
                          << buf.bytesused
                          << " raw_frame_size=" << raw_frame_size
                          << std::endl;
                xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                continue;
            }

            ret = aclrtMemcpyAsync(raw_frame_dev,
                                   raw_frame_size,
                                   v4l2_buffers[buf.index].start,
                                   buf.bytesused,
                                   ACL_MEMCPY_HOST_TO_DEVICE,
                                   stream);

            if (ret != ACL_SUCCESS) {
                std::cerr << "[ERROR] aclrtMemcpyAsync raw H2D failed, ret="
                          << ret << std::endl;
                xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                continue;
            }

            acldvppSetPicDescFormat(decoded_desc, PIXEL_FORMAT_YUYV_PACKED_422);
            acldvppSetPicDescSize(decoded_desc, raw_frame_size);
            acldvppSetPicDescData(decoded_desc, raw_frame_dev);
            acldvppSetPicDescWidth(decoded_desc, cam_w);
            acldvppSetPicDescHeight(decoded_desc, cam_h);
            acldvppSetPicDescWidthStride(decoded_desc, align_up(cam_w, 16));
            acldvppSetPicDescHeightStride(decoded_desc, align_up(cam_h, 2));

            ret = acldvppVpcCropAndPasteAsync(channel_desc,
                                              decoded_desc,
                                              model_desc,
                                              crop_area,
                                              paste_area,
                                              stream);

            if (ret != ACL_SUCCESS) {
                std::cerr << "[ERROR] acldvppVpcCropAndPasteAsync raw failed, ret="
                          << ret << std::endl;
                xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                continue;
            }

            // 恢复 decoded_desc 到 MJPEG decode 输出描述，避免后续状态污染。
            const uint32_t decoded_ws = align_up(cam_w, 128);
            const uint32_t decoded_hs = align_up(cam_h, 16);
            const uint32_t decoded_size = decoded_ws * decoded_hs * 3 / 2;

            acldvppSetPicDescData(decoded_desc, decoded_dev);
            acldvppSetPicDescSize(decoded_desc, decoded_size);
            acldvppSetPicDescFormat(decoded_desc, PIXEL_FORMAT_YUV_SEMIPLANAR_420);
            acldvppSetPicDescWidth(decoded_desc, cam_w);
            acldvppSetPicDescHeight(decoded_desc, cam_h);
            acldvppSetPicDescWidthStride(decoded_desc, decoded_ws);
            acldvppSetPicDescHeightStride(decoded_desc, decoded_hs);
        }

        // 3. 进入写入临界区：先把 seq 切成奇数，再覆盖图像数据。
        //    reader 的 seqlock 是 seq1(偶) -> copy -> seq2==seq1；
        //    若等 D2H 完成后再切 seq，reader 可能在 DMA 覆盖 shm_data 的
        //    过程中复制，读到半新半旧的撕裂帧。
        ++seq;
        const uint64_t writing_seq = seq * 2 + 1;
        const uint64_t ready_seq = seq * 2 + 2;

        __atomic_store_n(&shm_header->seq, writing_seq, __ATOMIC_RELEASE);

        // 4. D2H copy to shared memory image area
        ret = aclrtMemcpyAsync(shm_data,
                               MODEL_NV12_SIZE,
                               model_nv12_dev,
                               MODEL_NV12_SIZE,
                               ACL_MEMCPY_DEVICE_TO_HOST,
                               stream);

        if (ret != ACL_SUCCESS) {
            std::cerr << "[ERROR] aclrtMemcpyAsync D2H failed, ret="
                      << ret << std::endl;
            xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
            continue;
        }

        // 5. Single sync point
        ret = aclrtSynchronizeStream(stream);
        if (ret != ACL_SUCCESS) {
            std::cerr << "[ERROR] aclrtSynchronizeStream failed, ret="
                      << ret << std::endl;
            xioctl(v4l2_fd, VIDIOC_QBUF, &buf);
            continue;
        }

        // 6. 写入 metadata，最后把 seq 切成偶数，发布完整帧
        auto t1 = std::chrono::steady_clock::now();
        double pipeline_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();

        shm_header->timestamp_ns = timeval_to_ns(buf.timestamp);
        shm_header->camera_fps = publish_fps;
        shm_header->pipeline_ms = pipeline_ms;
        shm_header->width = MODEL_WIDTH;
        shm_header->height = MODEL_HEIGHT;
        shm_header->orig_width = cam_w;
        shm_header->orig_height = cam_h;
        shm_header->bytesused = MODEL_NV12_SIZE;
        shm_header->pad_left = pad_left;
        shm_header->pad_top = pad_top;
        shm_header->scale = scale;

        __atomic_store_n(&shm_header->seq, ready_seq, __ATOMIC_RELEASE);

        if (seq % 30 == 0) {
            std::cout << "[RUN] seq=" << seq
                      << " publish_fps=" << publish_fps
                      << " target_fps=" << CAM_FPS
                      << " dvpp_ms=" << pipeline_ms
                      << " jpeg_or_raw_bytes=" << buf.bytesused
                      << std::endl;
        }

        if (xioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[ERROR] VIDIOC_QBUF failed: "
                      << strerror(errno) << std::endl;
            break;
        }
    }

    std::cout << "[EXIT] cleanup..." << std::endl;
    cleanup();
    return 0;
}