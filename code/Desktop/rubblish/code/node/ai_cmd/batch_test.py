# -*- coding: utf-8 -*-
import os
import sys
import time
import glob
import cv2
import numpy as np
from aclruntime import InferenceSession, session_options, Tensor

from config import MODEL_PATH, MODEL_WIDTH, MODEL_HEIGHT, CLASS_NAMES
from inference_node import (
    preprocess_image,
    postprocess,
    check_color_by_hsv,
    CONF_THRESHOLD,
    IOU_THRESHOLD,
)

TEST_DIR = "models/test"
RESULT_DIR = "models/results"
os.makedirs(RESULT_DIR, exist_ok=True)


def draw_detections(img, detections):
    for x1, y1, x2, y2, conf, cls_id in detections:
        roi = img[y1:y2, x1:x2]
        cv_color = check_color_by_hsv(roi)
        model_label = CLASS_NAMES[cls_id]
        final_label = model_label
        if model_label in ["red", "green"]:
            if cv_color != "NONE":
                final_label = f"CV_{cv_color}"
            else:
                final_label = f"{model_label}?"
        box_color = (0, 0, 255) if "RED" in final_label else (0, 255, 0)
        if model_label == "line":
            box_color = (255, 255, 0)
        cv2.rectangle(img, (x1, y1), (x2, y2), box_color, 2)
        cv2.putText(img, f"{final_label}:{conf:.2f}", (x1, y1 - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, box_color, 2)


def main():
    print(f"Model: {MODEL_PATH}")
    print(f"Classes: {CLASS_NAMES}")
    print(f"Conf threshold: {CONF_THRESHOLD}, IOU threshold: {IOU_THRESHOLD}")
    print()

    model = InferenceSession(MODEL_PATH, 0, session_options())
    out_names = [meta.name for meta in model.get_outputs()]
    print(f"Output tensors: {out_names}")
    print()

    image_exts = ("*.jpg", "*.jpeg", "*.png", "*.bmp")
    image_files = []
    for ext in image_exts:
        image_files.extend(glob.glob(os.path.join(TEST_DIR, ext)))
    image_files.sort()

    if not image_files:
        print(f"No images found in {TEST_DIR}")
        return

    print(f"Found {len(image_files)} test images\n")

    total_stats = {"green": 0, "red": 0, "line": 0, "total": 0}
    total_time = 0.0

    for img_path in image_files:
        basename = os.path.basename(img_path)
        print(f"{'=' * 60}")
        print(f"Image: {basename}")
        print(f"{'=' * 60}")

        orig_img = cv2.imread(img_path)
        if orig_img is None:
            print(f"  [SKIP] Cannot read image")
            continue
        h, w = orig_img.shape[:2]
        print(f"  Size: {w}x{h}")

        t0 = time.perf_counter()
        input_tensor, orig_shape, trans_params = preprocess_image(
            orig_img, MODEL_WIDTH, MODEL_HEIGHT
        )
        raw_output = model.run(out_names, [Tensor(input_tensor)])
        raw_output_np = []
        for out in raw_output:
            out.to_host()
            raw_output_np.append(np.array(out))
        t1 = time.perf_counter()
        infer_ms = (t1 - t0) * 1000

        detections = postprocess(raw_output_np, orig_shape, trans_params)

        img_stats = {"green": 0, "red": 0, "line": 0}
        if detections:
            for x1, y1, x2, y2, conf, cls_id in detections:
                label = CLASS_NAMES[cls_id]
                img_stats[label] = img_stats.get(label, 0) + 1
                print(f"  [{label}] conf={conf:.3f} box=({x1},{y1},{x2},{y2}) "
                      f"size={x2 - x1}x{y2 - y1}")
            draw_detections(orig_img, detections)
        else:
            print(f"  [No detections]")

        result_path = os.path.join(RESULT_DIR, f"result_{basename}")
        cv2.imwrite(result_path, orig_img)

        for k, v in img_stats.items():
            total_stats[k] += v
        total_stats["total"] += len(detections) if detections else 0
        total_time += infer_ms

        det_count = len(detections) if detections else 0
        print(f"  Detections: {det_count}, Infer: {infer_ms:.1f}ms")
        print(f"  Saved: {result_path}")

    print()
    print(f"{'=' * 60}")
    print(f"SUMMARY")
    print(f"{'=' * 60}")
    print(f"Total images: {len(image_files)}")
    print(f"Total detections: {total_stats['total']}")
    for cls_name in CLASS_NAMES:
        print(f"  {cls_name}: {total_stats.get(cls_name, 0)}")
    avg_ms = total_time / len(image_files) if image_files else 0
    print(f"Average inference time: {avg_ms:.1f}ms ({1000 / avg_ms:.1f} FPS)")
    print(f"Results saved in: {RESULT_DIR}/")


if __name__ == "__main__":
    main()
