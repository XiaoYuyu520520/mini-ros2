#define ENABLE_DVPP_INTERFACE

#include <acl/acl.h>
#include <acl/ops/acl_dvpp.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>

// ================= 配置参数 =================
constexpr int DEVICE_ID = 0;
constexpr acldvppPixelFormat OUT_FORMAT = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
constexpr uint32_t WIDTH_ALIGN = 128;
constexpr uint32_t HEIGHT_ALIGN = 16;
// ===========================================

static uint32_t align_up_u32(uint32_t x, uint32_t align) {
    return (x + align - 1) / align * align;
}

static bool read_file(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cerr << "[ERROR] open input failed: " << path << std::endl;
        return false;
    }

    ifs.seekg(0, std::ios::end);
    std::streampos size = ifs.tellg();
    if (size <= 0) {
        std::cerr << "[ERROR] empty file: " << path << std::endl;
        return false;
    }

    data.resize(static_cast<size_t>(size));
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char*>(data.data()), data.size());
    return true;
}

static bool write_file(const std::string& path, const uint8_t* data, size_t size) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "[ERROR] open output failed: " << path << std::endl;
        return false;
    }

    ofs.write(reinterpret_cast<const char*>(data), size);
    return true;
}

static void mkdir_if_needed(const std::string& dir) {
    mkdir(dir.c_str(), 0755);
}

static std::vector<uint8_t> pack_nv12_without_stride(
    const std::vector<uint8_t>& src,
    uint32_t width,
    uint32_t height,
    uint32_t width_stride,
    uint32_t height_stride
) {
    std::vector<uint8_t> dst(static_cast<size_t>(width) * height * 3 / 2);

    uint8_t* out_y = dst.data();
    uint8_t* out_uv = dst.data() + static_cast<size_t>(width) * height;

    const uint8_t* in_y = src.data();
    const uint8_t* in_uv = src.data() + static_cast<size_t>(width_stride) * height_stride;

    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(
            out_y + static_cast<size_t>(row) * width,
            in_y + static_cast<size_t>(row) * width_stride,
            width
        );
    }

    for (uint32_t row = 0; row < height / 2; ++row) {
        std::memcpy(
            out_uv + static_cast<size_t>(row) * width,
            in_uv + static_cast<size_t>(row) * width_stride,
            width
        );
    }

    return dst;
}

int main(int argc, char** argv) {
    std::string input_path = "photos/1.jpg";
    int loop_count = 1;

    if (argc >= 2) {
        input_path = argv[1];
    }

    if (argc >= 3) {
        loop_count = std::stoi(argv[2]);
        if (loop_count <= 0) {
            loop_count = 1;
        }
    }

    std::cout << "[INFO] input      : " << input_path << std::endl;
    std::cout << "[INFO] loop_count : " << loop_count << std::endl;

    std::vector<uint8_t> jpeg_host;
    if (!read_file(input_path, jpeg_host)) {
        return 1;
    }

    std::cout << "[INFO] jpeg size  : " << jpeg_host.size() << " bytes" << std::endl;

    aclrtContext context = nullptr;
    aclrtStream stream = nullptr;
    acldvppChannelDesc* channel_desc = nullptr;
    acldvppPicDesc* output_desc = nullptr;

    void* input_dev = nullptr;
    void* output_dev = nullptr;

    bool acl_inited = false;
    bool device_set = false;

    auto cleanup = [&]() {
        if (output_desc != nullptr) {
            acldvppDestroyPicDesc(output_desc);
            output_desc = nullptr;
        }

        if (input_dev != nullptr) {
            acldvppFree(input_dev);
            input_dev = nullptr;
        }

        if (output_dev != nullptr) {
            acldvppFree(output_dev);
            output_dev = nullptr;
        }

        if (channel_desc != nullptr) {
            acldvppDestroyChannel(channel_desc);
            acldvppDestroyChannelDesc(channel_desc);
            channel_desc = nullptr;
        }

        if (stream != nullptr) {
            aclrtDestroyStream(stream);
            stream = nullptr;
        }

        if (context != nullptr) {
            aclrtDestroyContext(context);
            context = nullptr;
        }

        if (device_set) {
            aclrtResetDevice(DEVICE_ID);
        }

        if (acl_inited) {
            aclFinalize();
        }
    };

    auto check = [&](aclError ret, const std::string& name) -> bool {
        if (ret != ACL_SUCCESS) {
            std::cerr << "[ERROR] " << name << " failed, ret=" << ret << std::endl;
            return false;
        }
        return true;
    };

    aclError ret = ACL_SUCCESS;

    ret = aclInit(nullptr);
    if (!check(ret, "aclInit")) {
        cleanup();
        return 1;
    }
    acl_inited = true;

    ret = aclrtSetDevice(DEVICE_ID);
    if (!check(ret, "aclrtSetDevice")) {
        cleanup();
        return 1;
    }
    device_set = true;

    ret = aclrtCreateContext(&context, DEVICE_ID);
    if (!check(ret, "aclrtCreateContext")) {
        cleanup();
        return 1;
    }

    ret = aclrtCreateStream(&stream);
    if (!check(ret, "aclrtCreateStream")) {
        cleanup();
        return 1;
    }

    channel_desc = acldvppCreateChannelDesc();
    if (channel_desc == nullptr) {
        std::cerr << "[ERROR] acldvppCreateChannelDesc failed" << std::endl;
        cleanup();
        return 1;
    }

    ret = acldvppCreateChannel(channel_desc);
    if (!check(ret, "acldvppCreateChannel")) {
        cleanup();
        return 1;
    }

    uint32_t input_size = static_cast<uint32_t>(jpeg_host.size());

    ret = acldvppMalloc(&input_dev, input_size);
    if (!check(ret, "acldvppMalloc input")) {
        cleanup();
        return 1;
    }

    ret = aclrtMemcpy(
        input_dev,
        input_size,
        jpeg_host.data(),
        input_size,
        ACL_MEMCPY_HOST_TO_DEVICE
    );
    if (!check(ret, "aclrtMemcpy H2D")) {
        cleanup();
        return 1;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    int32_t components = 0;

    ret = acldvppJpegGetImageInfo(
        input_dev,
        input_size,
        &width,
        &height,
        &components
    );
    if (!check(ret, "acldvppJpegGetImageInfo")) {
        cleanup();
        return 1;
    }

    uint32_t out_width = width & ~1U;
    uint32_t out_height = height & ~1U;

    uint32_t width_stride = align_up_u32(out_width, WIDTH_ALIGN);
    uint32_t height_stride = align_up_u32(out_height, HEIGHT_ALIGN);

    std::cout << "[INFO] jpeg info  : "
              << width << "x" << height
              << ", components=" << components << std::endl;

    std::cout << "[INFO] out nv12   : "
              << out_width << "x" << out_height
              << ", stride=" << width_stride << "x" << height_stride << std::endl;

    uint32_t predicted_output_size = 0;

    ret = acldvppJpegPredictDecSize(
        input_dev,
        input_size,
        OUT_FORMAT,
        &predicted_output_size
    );
    if (!check(ret, "acldvppJpegPredictDecSize")) {
        cleanup();
        return 1;
    }

    uint32_t calculated_output_size = width_stride * height_stride * 3 / 2;
    uint32_t output_size =
        predicted_output_size > calculated_output_size
            ? predicted_output_size
            : calculated_output_size;

    std::cout << "[INFO] predicted output size : " << predicted_output_size << std::endl;
    std::cout << "[INFO] calculated output size: " << calculated_output_size << std::endl;
    std::cout << "[INFO] allocated output size : " << output_size << std::endl;

    ret = acldvppMalloc(&output_dev, output_size);
    if (!check(ret, "acldvppMalloc output")) {
        cleanup();
        return 1;
    }

    output_desc = acldvppCreatePicDesc();
    if (output_desc == nullptr) {
        std::cerr << "[ERROR] acldvppCreatePicDesc failed" << std::endl;
        cleanup();
        return 1;
    }

    acldvppSetPicDescData(output_desc, output_dev);
    acldvppSetPicDescSize(output_desc, output_size);
    acldvppSetPicDescFormat(output_desc, OUT_FORMAT);
    acldvppSetPicDescWidth(output_desc, out_width);
    acldvppSetPicDescHeight(output_desc, out_height);
    acldvppSetPicDescWidthStride(output_desc, width_stride);
    acldvppSetPicDescHeightStride(output_desc, height_stride);

    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < loop_count; ++i) {
        ret = acldvppJpegDecodeAsync(
            channel_desc,
            input_dev,
            input_size,
            output_desc,
            stream
        );

        if (!check(ret, "acldvppJpegDecodeAsync")) {
            cleanup();
            return 1;
        }

        ret = aclrtSynchronizeStream(stream);
        if (!check(ret, "aclrtSynchronizeStream")) {
            cleanup();
            return 1;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << "[INFO] decode loops: " << loop_count << std::endl;
    std::cout << "[INFO] total time  : " << ms << " ms" << std::endl;
    std::cout << "[INFO] avg time    : " << ms / loop_count << " ms/frame" << std::endl;
    std::cout << "[INFO] approx fps  : " << 1000.0 / (ms / loop_count) << std::endl;

    std::vector<uint8_t> full_host(output_size);

    ret = aclrtMemcpy(
        full_host.data(),
        output_size,
        output_dev,
        output_size,
        ACL_MEMCPY_DEVICE_TO_HOST
    );
    if (!check(ret, "aclrtMemcpy D2H")) {
        cleanup();
        return 1;
    }

    mkdir_if_needed("out");

    write_file("out/decoded_full.nv12", full_host.data(), full_host.size());

    std::vector<uint8_t> packed = pack_nv12_without_stride(
        full_host,
        out_width,
        out_height,
        width_stride,
        height_stride
    );

    write_file("out/decoded_packed.nv12", packed.data(), packed.size());

    std::ofstream meta("out/meta.txt");
    meta << "input=" << input_path << "\n";
    meta << "jpeg_width=" << width << "\n";
    meta << "jpeg_height=" << height << "\n";
    meta << "out_width=" << out_width << "\n";
    meta << "out_height=" << out_height << "\n";
    meta << "width_stride=" << width_stride << "\n";
    meta << "height_stride=" << height_stride << "\n";
    meta << "full_size=" << full_host.size() << "\n";
    meta << "packed_size=" << packed.size() << "\n";

    std::cout << "[OK] decode success" << std::endl;
    std::cout << "[OK] output full   : out/decoded_full.nv12" << std::endl;
    std::cout << "[OK] output packed : out/decoded_packed.nv12" << std::endl;
    std::cout << "[OK] meta          : out/meta.txt" << std::endl;

    cleanup();
    return 0;
}
