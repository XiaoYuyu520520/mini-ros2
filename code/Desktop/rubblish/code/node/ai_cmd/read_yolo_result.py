# -*- coding: utf-8 -*-
"""Read YOLO results from yolo_result_shm without unlinking it."""
import time
import struct
from multiprocessing import shared_memory, resource_tracker

RESULT_SHM_NAME = "yolo_result_shm"
CLASS_NAMES = ["green", "red", "line"]

RESULT_HEADER_FMT = "<QQdIQ"
DET_FMT = "<iiiifI"
RESULT_HEADER_SIZE = struct.calcsize(RESULT_HEADER_FMT)
DET_SIZE = struct.calcsize(DET_FMT)
MAX_DETECTIONS = 32


def unregister_from_resource_tracker(shm):
    try:
        resource_tracker.unregister(shm._name, "shared_memory")
    except Exception:
        pass


def attach_shared_memory_no_unlink(name):
    shm = shared_memory.SharedMemory(name=name)
    unregister_from_resource_tracker(shm)
    return shm


def read_once(shm):
    h1 = struct.unpack_from(RESULT_HEADER_FMT, shm.buf, 0)
    seq1, timestamp_ns, infer_fps, det_count, camera_seq = h1

    if seq1 == 0 or seq1 % 2 == 1:
        return None

    det_count = min(int(det_count), MAX_DETECTIONS)
    detections = []
    offset = RESULT_HEADER_SIZE

    for _ in range(det_count):
        x1, y1, x2, y2, conf, cls_id = struct.unpack_from(DET_FMT, shm.buf, offset)
        cls_id = int(cls_id)
        label = CLASS_NAMES[cls_id] if cls_id < len(CLASS_NAMES) else str(cls_id)
        detections.append({
            "label": label,
            "conf": float(conf),
            "box": [int(x1), int(y1), int(x2), int(y2)],
        })
        offset += DET_SIZE

    h2 = struct.unpack_from(RESULT_HEADER_FMT, shm.buf, 0)
    seq2 = int(h2[0])

    if seq1 != seq2 or seq2 % 2 == 1:
        return None

    return {
        "result_seq": seq2 // 2,
        "camera_seq": int(camera_seq),
        "timestamp_ns": int(timestamp_ns),
        "infer_fps": float(infer_fps),
        "detections": detections,
    }


def main():
    print("等待 yolo_result_shm ...")
    shm = None

    try:
        while True:
            try:
                shm = attach_shared_memory_no_unlink(RESULT_SHM_NAME)
                break
            except FileNotFoundError:
                time.sleep(0.1)

        print("已连接结果共享内存")
        last_seq = 0

        while True:
            result = read_once(shm)
            if result is None:
                time.sleep(0.005)
                continue

            if result["result_seq"] != last_seq:
                last_seq = result["result_seq"]
                print(
                    f"seq={result['result_seq']} "
                    f"cam_seq={result['camera_seq']} "
                    f"fps={result['infer_fps']:.2f} "
                    f"dets={len(result['detections'])} "
                    f"{result['detections']}"
                )

            time.sleep(0.01)

    except KeyboardInterrupt:
        print("\n退出读取")

    finally:
        if shm is not None:
            shm.close()


if __name__ == "__main__":
    main()
