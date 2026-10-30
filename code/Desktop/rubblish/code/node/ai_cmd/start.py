#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
One-command real-time camera inference.

Pipeline:
  dvpp_camera_producer -> dvpp_cam_nv12 shared memory -> OM inference -> yolo_result_shm

Usage:
  python3 start.py
  python3 start.py --device /dev/video1
  python3 start.py --no-hsv
  python3 start.py --force-build

Open another terminal and run:
  python3 view.py
"""
import argparse
import os
import signal
import struct
import subprocess
import sys
import time
from multiprocessing import resource_tracker, shared_memory

import cv2
import numpy as np
from aclruntime import InferenceSession, Tensor, session_options

from inference_node import (
    CLASS_NAMES,
    MODEL_HEIGHT,
    MODEL_PATH,
    MODEL_WIDTH,
    correct_detections_by_hsv,
    postprocess,
)

sys.stdout.reconfigure(line_buffering=True)

IMAGE_SHM_NAME = "dvpp_cam_nv12"
IMAGE_HEADER_SIZE = 4096
NV12_SIZE = MODEL_WIDTH * MODEL_HEIGHT * 3 // 2
IMAGE_HEADER_FMT = "<QQddIIIIIIIf"

RESULT_SHM_NAME = "yolo_result_shm"
RESULT_SHM_SIZE = 4096
MAX_DETECTIONS = 32
RESULT_HEADER_FMT = "<QQdIQ"
DET_FMT = "<iiiifI"
RESULT_HEADER_SIZE = struct.calcsize(RESULT_HEADER_FMT)
DET_SIZE = struct.calcsize(DET_FMT)

PRODUCER_BIN = "./dvpp_camera_producer"
PRODUCER_SRC = "./dvpp_camera_producer.cpp"
BUILD_SCRIPT = "./build.sh"
PRINT_INTERVAL = 1.0
SLEEP_WHEN_NO_FRAME = 0.0005
COPY_INPUT_FRAME = True


def unregister_from_resource_tracker(shm):
    try:
        resource_tracker.unregister(shm._name, "shared_memory")
    except Exception:
        pass


def wait_for_shm(name, timeout=10.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            shm = shared_memory.SharedMemory(name=name)
            unregister_from_resource_tracker(shm)
            return shm
        except FileNotFoundError:
            time.sleep(0.05)
    raise FileNotFoundError(f"shared memory not found: {name}")


def read_image_header(buf):
    values = struct.unpack_from(IMAGE_HEADER_FMT, buf, 0)
    return {
        "seq": int(values[0]),
        "timestamp_ns": int(values[1]),
        "camera_fps": float(values[2]),
        "pipeline_ms": float(values[3]),
        "width": int(values[4]),
        "height": int(values[5]),
        "orig_width": int(values[6]),
        "orig_height": int(values[7]),
        "bytesused": int(values[8]),
        "pad_left": int(values[9]),
        "pad_top": int(values[10]),
        "scale": float(values[11]),
    }


def read_stable_frame(image_shm, frame_view, last_ready_seq):
    header1 = read_image_header(image_shm.buf)
    seq1 = header1["seq"]
    if seq1 == 0 or seq1 == last_ready_seq or seq1 % 2 == 1:
        return None, None
    if header1["width"] != MODEL_WIDTH or header1["height"] != MODEL_HEIGHT:
        raise RuntimeError(f"invalid model frame size: {header1['width']}x{header1['height']}")
    if header1["bytesused"] != NV12_SIZE:
        raise RuntimeError(f"invalid NV12 size: {header1['bytesused']}, expected {NV12_SIZE}")

    if COPY_INPUT_FRAME:
        nv12 = np.empty((NV12_SIZE,), dtype=np.uint8)
        nv12[:] = frame_view
    else:
        nv12 = np.ndarray((NV12_SIZE,), dtype=np.uint8, buffer=image_shm.buf, offset=IMAGE_HEADER_SIZE)

    header2 = read_image_header(image_shm.buf)
    if seq1 != header2["seq"] or header2["seq"] % 2 == 1:
        return None, None
    header2["camera_seq"] = header2["seq"] // 2
    return header2, nv12


def create_result_shm():
    try:
        old = shared_memory.SharedMemory(name=RESULT_SHM_NAME)
        old.close()
        old.unlink()
    except FileNotFoundError:
        pass

    shm = shared_memory.SharedMemory(name=RESULT_SHM_NAME, create=True, size=RESULT_SHM_SIZE)
    shm.buf[:] = b"\x00" * RESULT_SHM_SIZE
    unregister_from_resource_tracker(shm)
    return shm


def write_results(result_shm, result_seq, timestamp_ns, infer_fps, camera_seq, detections):
    if detections is None:
        detections = []
    det_count = min(len(detections), MAX_DETECTIONS)

    struct.pack_into(
        RESULT_HEADER_FMT,
        result_shm.buf,
        0,
        result_seq * 2 + 1,
        int(timestamp_ns),
        float(infer_fps),
        0,
        int(camera_seq),
    )

    offset = RESULT_HEADER_SIZE
    for i in range(det_count):
        x1, y1, x2, y2, conf, cls_id = detections[i]
        struct.pack_into(
            DET_FMT,
            result_shm.buf,
            offset,
            int(x1),
            int(y1),
            int(x2),
            int(y2),
            float(conf),
            int(cls_id),
        )
        offset += DET_SIZE

    struct.pack_into(
        RESULT_HEADER_FMT,
        result_shm.buf,
        0,
        result_seq * 2 + 2,
        int(timestamp_ns),
        float(infer_fps),
        int(det_count),
        int(camera_seq),
    )


def nv12_to_bgr(nv12):
    return cv2.cvtColor(nv12.reshape((MODEL_HEIGHT * 3 // 2, MODEL_WIDTH)), cv2.COLOR_YUV2BGR_NV12)


def nv12_to_rgb(nv12):
    return cv2.cvtColor(nv12_to_bgr(nv12), cv2.COLOR_BGR2RGB)


def needs_build(force_build=False):
    if force_build or not os.path.exists(PRODUCER_BIN):
        return True
    try:
        bin_mtime = os.path.getmtime(PRODUCER_BIN)
        src_mtime = os.path.getmtime(PRODUCER_SRC)
        script_mtime = os.path.getmtime(BUILD_SCRIPT)
        return src_mtime > bin_mtime or script_mtime > bin_mtime
    except OSError:
        return True


def build_producer(force_build=False):
    if not needs_build(force_build):
        print("Build skipped: dvpp_camera_producer is up to date")
        return
    print("Building dvpp_camera_producer...")
    result = subprocess.run(["bash", BUILD_SCRIPT], text=True)
    if result.returncode != 0:
        raise RuntimeError("failed to build dvpp_camera_producer")
    print("Build OK")


def start_producer(device):
    cmd = [PRODUCER_BIN]
    if device and device != "auto":
        cmd += ["--device", device]
    print("Starting camera producer:", " ".join(cmd))
    # 只保留这一行，删除所有绑定代码
    return subprocess.Popen(cmd)


def stop_process(proc):
    if proc is None or proc.poll() is not None:
        return
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def parse_args():
    parser = argparse.ArgumentParser(description="Run real-time camera inference")
    parser.add_argument("--device", default="auto", help="camera device, e.g. /dev/video0")
    parser.add_argument("--no-hsv", action="store_true", help="disable HSV red/green correction")
    parser.add_argument("--hsv-debug", action="store_true", help="print HSV correction details")
    parser.add_argument("--no-build", action="store_true", help="do not build C++ producer before starting")
    parser.add_argument("--force-build", action="store_true", help="rebuild C++ producer even if binary exists")
    return parser.parse_args()


def main():
    args = parse_args()
    producer = None
    image_shm = None
    result_shm = None
    frame_view = None

    try:
        if not args.no_build:
            build_producer(force_build=args.force_build)

        producer = start_producer(args.device)

        print(f"Connecting to camera shared memory: {IMAGE_SHM_NAME}")
        image_shm = wait_for_shm(IMAGE_SHM_NAME, timeout=10.0)
        frame_view = np.ndarray((NV12_SIZE,), dtype=np.uint8, buffer=image_shm.buf, offset=IMAGE_HEADER_SIZE)
        print("Camera shared memory connected")

        result_shm = create_result_shm()
        print(f"Result shared memory created: {RESULT_SHM_NAME}")

        print(f"Loading model: {MODEL_PATH}")
        model = InferenceSession(MODEL_PATH, 0, session_options())
        out_names = [meta.name for meta in model.get_outputs()]
        print(f"Model loaded, outputs: {out_names}")
        print(f"HSV correction: {not args.no_hsv}")
        print("Running inference. Open another terminal: python3 view.py")

        last_ready_seq = 0
        result_seq = 0
        ok_count = 0
        last_print_time = time.time()
        fps_window = []

        while True:
            if producer is not None and producer.poll() is not None:
                raise RuntimeError(f"camera producer exited with code {producer.returncode}")

            header, nv12 = read_stable_frame(image_shm, frame_view, last_ready_seq)
            if header is None:
                time.sleep(SLEEP_WHEN_NO_FRAME)
                continue

            last_ready_seq = header["seq"]
            camera_seq = header["camera_seq"]
            orig_shape = (header["orig_height"], header["orig_width"])
            trans_params = (header["scale"], header["pad_left"], header["pad_top"])

            rgb = nv12_to_rgb(nv12)
            input_tensor = rgb[np.newaxis, :]

            t0 = time.perf_counter()
            raw_outputs = model.run(out_names, [Tensor(input_tensor)])
            t1 = time.perf_counter()
            raw_outputs_np = []
            for out in raw_outputs:
                out.to_host()
                raw_outputs_np.append(np.array(out))
            t2 = time.perf_counter()

            detections = postprocess(raw_outputs_np, orig_shape, trans_params)

            if not args.no_hsv:
                model_bgr = cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)
                detections = correct_detections_by_hsv(
                    detections, model_bgr, trans_params, debug=args.hsv_debug
                )
            t3 = time.perf_counter()

            result_seq += 1
            ok_count += 1
            now = time.time()
            fps_window.append(now)
            fps_window = [t for t in fps_window if now - t <= 2.0]
            infer_fps = len(fps_window) / max(fps_window[-1] - fps_window[0], 0.01)

            write_results(
                result_shm,
                result_seq,
                header["timestamp_ns"],
                infer_fps,
                camera_seq,
                detections,
            )

            if now - last_print_time >= PRINT_INTERVAL:
                det_count = 0 if detections is None else len(detections)
                loop_fps = ok_count / max(now - last_print_time, 1e-6)
                print(
                    f"INFER {loop_fps:.1f}fps | cam {header['camera_fps']:.1f}fps | "
                    f"dvpp {header['pipeline_ms']:.1f}ms | "
                    f"infer {(t1 - t0) * 1000:.1f}ms | "
                    f"to_host {(t2 - t1) * 1000:.1f}ms | "
                    f"post+hsv {(t3 - t2) * 1000:.1f}ms | dets {det_count}"
                )
                if detections:
                    for x1, y1, x2, y2, conf, cls_id in detections[:10]:
                        label = CLASS_NAMES[cls_id] if cls_id < len(CLASS_NAMES) else str(cls_id)
                        print(f"  {label} {conf:.3f} orig_box=({x1},{y1},{x2},{y2})")
                ok_count = 0
                last_print_time = now

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        frame_view = None
        if image_shm is not None:
            try:
                image_shm.close()
            except Exception:
                pass
        if result_shm is not None:
            try:
                result_shm.close()
                result_shm.unlink()
            except Exception:
                pass
        stop_process(producer)
        print("Exited.")


if __name__ == "__main__":
    main()
