#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Display the real-time camera frame and draw detection boxes.

The camera shared memory stores a 640x640 letterboxed NV12 frame.
The result shared memory stores boxes in original camera coordinates.
This viewer converts each result box back to 640x640 coordinates before drawing.
"""
import struct
import sys
import time
from multiprocessing import resource_tracker, shared_memory

import cv2
import numpy as np

try:
    import pygame
except ImportError:
    print("Install pygame first: pip install pygame")
    sys.exit(1)

from inference_node import CLASS_NAMES, MODEL_HEIGHT, MODEL_WIDTH, orig_box_to_model_box

sys.stdout.reconfigure(line_buffering=True)

IMAGE_SHM_NAME = "dvpp_cam_nv12"
IMAGE_HEADER_SIZE = 4096
NV12_SIZE = MODEL_WIDTH * MODEL_HEIGHT * 3 // 2
IMAGE_HEADER_FMT = "<QQddIIIIIIIf"

RESULT_SHM_NAME = "yolo_result_shm"
MAX_DETECTIONS = 32
RESULT_HEADER_FMT = "<QQdIQ"
DET_FMT = "<iiiifI"
RESULT_HEADER_SIZE = struct.calcsize(RESULT_HEADER_FMT)
DET_SIZE = struct.calcsize(DET_FMT)

WINDOW_WIDTH = MODEL_WIDTH
WINDOW_HEIGHT = MODEL_HEIGHT
FONT_SIZE = 18

# BGR colors for OpenCV drawing.
COLORS = {
    0: (0, 255, 0),      # green
    1: (0, 0, 255),      # red
    2: (255, 255, 0),    # line / cyan
}


def unregister_from_resource_tracker(shm):
    try:
        resource_tracker.unregister(shm._name, "shared_memory")
    except Exception:
        pass


def shm_connect(name, timeout=10.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            shm = shared_memory.SharedMemory(name=name)
            unregister_from_resource_tracker(shm)
            return shm
        except FileNotFoundError:
            time.sleep(0.05)
    return None


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


def read_frame(image_shm, frame_view, last_seq):
    header1 = read_image_header(image_shm.buf)
    seq1 = header1["seq"]
    if seq1 == 0 or seq1 == last_seq or seq1 % 2 == 1:
        return None, None, None
    if header1["bytesused"] != NV12_SIZE:
        return None, None, None

    nv12 = np.empty((NV12_SIZE,), dtype=np.uint8)
    nv12[:] = frame_view

    header2 = read_image_header(image_shm.buf)
    if seq1 != header2["seq"] or header2["seq"] % 2 == 1:
        return None, None, None
    return header2, nv12, seq1


def read_results(result_shm):
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


def draw_text(bgr, text, x, y, color=(255, 255, 255)):
    cv2.putText(bgr, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 0), 3)
    cv2.putText(bgr, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)


def draw_detections(bgr, detections, image_header):
    trans_params = (image_header["scale"], image_header["pad_left"], image_header["pad_top"])
    for x1, y1, x2, y2, conf, cls_id in detections:
        mx1, my1, mx2, my2 = orig_box_to_model_box((x1, y1, x2, y2), trans_params)
        if mx2 <= mx1 or my2 <= my1:
            continue
        color = COLORS.get(int(cls_id), (255, 255, 255))
        cv2.rectangle(bgr, (mx1, my1), (mx2, my2), color, 2)
        label = CLASS_NAMES[cls_id] if 0 <= cls_id < len(CLASS_NAMES) else str(cls_id)
        draw_text(bgr, f"{label} {conf:.2f}", mx1, max(16, my1 - 6), color)


def main():
    print("Connecting to shared memories...")

    image_shm = shm_connect(IMAGE_SHM_NAME)
    if image_shm is None:
        print(f"Cannot find {IMAGE_SHM_NAME}. Start inference first: python3 start.py")
        sys.exit(1)

    result_shm = shm_connect(RESULT_SHM_NAME)
    if result_shm is None:
        image_shm.close()
        print(f"Cannot find {RESULT_SHM_NAME}. Start inference first: python3 start.py")
        sys.exit(1)

    frame_view = np.ndarray((NV12_SIZE,), dtype=np.uint8, buffer=image_shm.buf, offset=IMAGE_HEADER_SIZE)

    print(f"Connected. Camera={IMAGE_SHM_NAME}, Results={RESULT_SHM_NAME}")
    print("Press q or ESC to exit")

    pygame.init()
    screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
    pygame.display.set_caption("Camera Inference View")
    font = pygame.font.SysFont("monospace", FONT_SIZE)
    clock = pygame.time.Clock()

    last_frame_seq = 0
    last_result_seq = -1
    last_detections = []
    last_infer_fps = 0.0
    last_camera_seq = 0
    frame_count = 0
    fps_timer = time.time()
    display_fps = 0
    running = True

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key in (pygame.K_ESCAPE, pygame.K_q):
                running = False

        image_header, nv12, seq = read_frame(image_shm, frame_view, last_frame_seq)
        if nv12 is None:
            time.sleep(0.005)
            clock.tick(60)
            continue
        last_frame_seq = seq

        bgr = cv2.cvtColor(nv12.reshape((MODEL_HEIGHT * 3 // 2, MODEL_WIDTH)), cv2.COLOR_YUV2BGR_NV12)

        detections, result_seq, infer_fps, camera_seq = read_results(result_shm)
        if detections is not None:
            last_detections = detections
            last_infer_fps = infer_fps
            last_camera_seq = camera_seq
            if result_seq != last_result_seq:
                last_result_seq = result_seq
                print(f"[{result_seq}] cam_seq={camera_seq} detections={len(detections)}")
                for x1, y1, x2, y2, conf, cls_id in detections:
                    label = CLASS_NAMES[cls_id] if 0 <= cls_id < len(CLASS_NAMES) else str(cls_id)
                    trans_params = (image_header["scale"], image_header["pad_left"], image_header["pad_top"])
                    mx1, my1, mx2, my2 = orig_box_to_model_box((x1, y1, x2, y2), trans_params)
                    print(
                        f"  {label} {conf:.3f} "
                        f"orig=({x1},{y1},{x2},{y2}) model=({mx1},{my1},{mx2},{my2})"
                    )

        if last_detections:
            draw_detections(bgr, last_detections, image_header)

        frame_count += 1
        now = time.time()
        if now - fps_timer >= 1.0:
            display_fps = frame_count
            frame_count = 0
            fps_timer = now

        overlay = (
            f"view {display_fps}fps | infer {last_infer_fps:.1f}fps | "
            f"dets {len(last_detections)} | cam_seq {last_camera_seq}"
        )
        draw_text(bgr, overlay, 8, 22, (255, 255, 0))

        rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        surf = pygame.surfarray.make_surface(rgb.transpose(1, 0, 2))
        screen.blit(surf, (0, 0))
        pygame.display.flip()
        clock.tick(60)

    pygame.quit()
    image_shm.close()
    result_shm.close()
    print("Viewer closed.")


if __name__ == "__main__":
    main()
