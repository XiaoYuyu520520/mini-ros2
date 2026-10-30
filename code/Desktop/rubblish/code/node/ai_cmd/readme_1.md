# test_om 实时摄像头推理版本

这个包已经清理为一条主流程：

```text
摄像头 /dev/videoX
  -> dvpp_camera_producer.cpp 采集 + DVPP JPEGD/VPC + 640x640 NV12 letterbox
  -> dvpp_cam_nv12 共享内存
  -> start.py OM 推理 + 后处理 + HSV 红绿校正
  -> yolo_result_shm 共享内存
  -> view.py 显示画面并画框
```

## 关键修复

1. **修复摄像头显示画框坐标错位问题**
   - 推理结果是原始摄像头坐标，例如 1280x720。
   - `view.py` 显示的是 640x640 letterbox 图像。
   - 新版 `view.py` 会先把原始坐标转换回 640x640 letterbox 坐标，再画框。

2. **修复 OpenCV 画框颜色**
   - OpenCV 使用 BGR，不是 RGB。
   - 红色框已修正为 `(0, 0, 255)`。

3. **清理重复入口**
   - 保留 `start.py` 作为唯一实时推理入口。
   - 删除旧版重复推理脚本和无效调试脚本。

4. **避免旧二进制影响运行**
   - 包内不再保留旧的 `dvpp_camera_producer` 二进制。
   - `start.py` 会在二进制不存在、源码更新或使用 `--force-build` 时重新编译。

## 环境要求

- Ascend CANN / Ascend Toolkit 环境可用
- Python 包：`numpy`, `opencv-python`, `pygame`, `aclruntime`
- 摄像头设备可用，例如 `/dev/video0`

## 运行方法

### 1. 一键启动摄像头推理

```bash
cd test_om
python3 start.py
```

指定摄像头：

```bash
python3 start.py --device /dev/video1
```

强制重新编译 C++ 采集程序：

```bash
python3 start.py --force-build
```

关闭 HSV 红绿校正：

```bash
python3 start.py --no-hsv
```

### 2. 打开显示窗口

另开一个终端：

```bash
cd test_om
python3 view.py
```

显示窗口里画的是 640x640 摄像头 letterbox 画面，框会自动转换到正确位置。

### 3. 只读取检测结果

另开一个终端：

```bash
python3 read_yolo_result.py
```

如果这里能看到 `dets > 0`，说明推理结果正常；如果显示窗口框位置不对，则重点看 `view.py` 的坐标转换。

### 4. 离线图片测试

```bash
python3 batch_test.py
```

测试图片在：

```text
models/test/
```

输出结果会保存到：

```text
models/results/
```

## 当前保留文件

```text
config.py                    模型路径、输入尺寸、类别名
inference_node.py            预处理、后处理、坐标转换、HSV 校正
start.py                     实时摄像头推理主入口
view.py                      摄像头显示与画框
read_yolo_result.py          读取检测结果共享内存
batch_test.py                离线图片测试
build.sh                     编译 C++ 采集程序
dvpp_camera_producer.cpp     摄像头采集 + DVPP 转换
run_inference.sh             启动 start.py
run_view.sh                  启动 view.py
run_reader.sh                启动 read_yolo_result.py
models/v5/best_yolov5_aipp.om
models/test/                 离线测试图片
```
