# -*- coding: utf-8 -*-
"""Common model preprocessing / postprocessing utilities."""
import cv2
import numpy as np
from config import MODEL_PATH, MODEL_WIDTH, MODEL_HEIGHT, CLASS_NAMES

# 与 cmd/cmd_vel转发控制.py 保持一致：最低按红/绿灯 0.25 输出，
# 再由控制器对红绿灯(0.25)/斑马线(0.50)分别过滤。否则推理端 0.5 会把控制端的 0.25 架空。
CONF_THRESHOLD = 0.25
IOU_THRESHOLD = 0.35


def sigmoid(x):
    return 1 / (1 + np.exp(-x))


def clip_box(box, width, height):
    x1, y1, x2, y2 = box
    x1 = max(0, min(width - 1, int(round(x1))))
    y1 = max(0, min(height - 1, int(round(y1))))
    x2 = max(0, min(width - 1, int(round(x2))))
    y2 = max(0, min(height - 1, int(round(y2))))
    return x1, y1, x2, y2


def orig_box_to_model_box(box, trans_params):
    """Convert original camera/image coordinates to 640x640 letterbox coordinates."""
    x1, y1, x2, y2 = box
    scale, pad_left, pad_top = trans_params
    return clip_box(
        (
            x1 * scale + pad_left,
            y1 * scale + pad_top,
            x2 * scale + pad_left,
            y2 * scale + pad_top,
        ),
        MODEL_WIDTH,
        MODEL_HEIGHT,
    )


def model_box_to_orig_box(box, orig_shape, trans_params):
    """Convert 640x640 letterbox coordinates to original image coordinates."""
    x1, y1, x2, y2 = box
    orig_h, orig_w = orig_shape
    scale, pad_left, pad_top = trans_params
    return clip_box(
        (
            (x1 - pad_left) / scale,
            (y1 - pad_top) / scale,
            (x2 - pad_left) / scale,
            (y2 - pad_top) / scale,
        ),
        orig_w,
        orig_h,
    )


def check_color_by_hsv(roi_bgr, debug=False):
    """Return RED/GREEN/NONE using a conservative HSV check on a BGR ROI."""
    if roi_bgr is None or roi_bgr.size == 0:
        return "NONE"
    h, w = roi_bgr.shape[:2]
    if h < 4 or w < 4:
        return "NONE"

    # Ignore border pixels; boxes often contain background around traffic lights/targets.
    cx1 = int(w * 0.20)
    cx2 = int(w * 0.80)
    cy1 = int(h * 0.15)
    cy2 = int(h * 0.85)
    roi = roi_bgr[cy1:cy2, cx1:cx2]
    if roi.size == 0:
        roi = roi_bgr

    hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
    hue = hsv[:, :, 0]
    sat = hsv[:, :, 1]
    val = hsv[:, :, 2]

    valid = (sat >= 70) & (val >= 70)
    valid_count = int(np.count_nonzero(valid))
    if valid_count == 0:
        return "NONE"

    red_mask = (((hue <= 12) | (hue >= 165)) & valid)
    green_mask = ((hue >= 35) & (hue <= 90) & valid)
    red_count = int(np.count_nonzero(red_mask))
    green_count = int(np.count_nonzero(green_mask))

    if debug:
        red_ratio = red_count / max(valid_count, 1)
        green_ratio = green_count / max(valid_count, 1)
        print(
            f"[HSV] red={red_count} ({red_ratio:.3f}), "
            f"green={green_count} ({green_ratio:.3f}), valid={valid_count}"
        )

    min_pixels = max(8, int(valid_count * 0.03))
    if red_count >= min_pixels and red_count >= green_count * 0.75:
        return "RED"
    if green_count >= min_pixels and green_count >= red_count * 1.50:
        return "GREEN"
    return "NONE"


def correct_detections_by_hsv(detections, model_bgr, trans_params, debug=False):
    """Correct red/green class labels using the 640x640 camera frame shown by view.py."""
    if not detections:
        return detections

    corrected = []
    for det in detections:
        x1, y1, x2, y2, conf, cls_id = det
        cls_id = int(cls_id)
        final_cls_id = cls_id

        if 0 <= cls_id < len(CLASS_NAMES) and CLASS_NAMES[cls_id] in ("red", "green"):
            mx1, my1, mx2, my2 = orig_box_to_model_box((x1, y1, x2, y2), trans_params)
            if mx2 > mx1 and my2 > my1:
                hsv_color = check_color_by_hsv(model_bgr[my1:my2, mx1:mx2], debug=debug)
                if hsv_color == "RED" and "red" in CLASS_NAMES:
                    final_cls_id = CLASS_NAMES.index("red")
                elif hsv_color == "GREEN" and "green" in CLASS_NAMES:
                    final_cls_id = CLASS_NAMES.index("green")
                if debug:
                    before = CLASS_NAMES[cls_id] if cls_id < len(CLASS_NAMES) else str(cls_id)
                    after = CLASS_NAMES[final_cls_id] if final_cls_id < len(CLASS_NAMES) else str(final_cls_id)
                    print(f"  hsv: {before} -> {hsv_color} -> {after}")

        corrected.append((int(x1), int(y1), int(x2), int(y2), float(conf), int(final_cls_id)))
    return corrected


def preprocess_image(image, input_w, input_h):
    """BGR image -> RGB letterbox tensor [1,input_h,input_w,3] uint8."""
    h, w = image.shape[:2]
    scale = min(input_w / w, input_h / h)
    nw, nh = int(w * scale), int(h * scale)

    rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    resized = cv2.resize(rgb, (nw, nh))
    padded = np.full((input_h, input_w, 3), 114, dtype=np.uint8)
    top, left = (input_h - nh) // 2, (input_w - nw) // 2
    padded[top:top + nh, left:left + nw] = resized
    return padded[np.newaxis, :], (h, w), (scale, left, top)


def postprocess(raw_outputs, orig_shape, trans_params):
    """
    Model output is [1,25200,8] with decoded x,y,w,h in 640x640 letterbox space.
    Return detections as (x1,y1,x2,y2,score,class_id) in original image coordinates.
    """
    output = raw_outputs[0][0]
    xc = output[:, 0]
    yc = output[:, 1]
    bw = output[:, 2]
    bh = output[:, 3]
    obj = sigmoid(output[:, 4])
    cls = sigmoid(output[:, 5:5 + len(CLASS_NAMES)])

    scores = obj * np.max(cls, axis=1)
    class_ids = np.argmax(cls, axis=1)

    mask = scores > CONF_THRESHOLD
    if not np.any(mask):
        return []

    xc = xc[mask]
    yc = yc[mask]
    bw = bw[mask]
    bh = bh[mask]
    scores = scores[mask]
    class_ids = class_ids[mask]

    x1 = xc - bw / 2
    y1 = yc - bh / 2
    x2 = xc + bw / 2
    y2 = yc + bh / 2

    nms_boxes = np.stack([x1, y1, bw, bh], axis=1).tolist()
    indices = cv2.dnn.NMSBoxes(nms_boxes, scores.tolist(), CONF_THRESHOLD, IOU_THRESHOLD)
    if len(indices) == 0:
        return []

    results = []
    for i in indices.flatten():
        ox1, oy1, ox2, oy2 = model_box_to_orig_box(
            (x1[i], y1[i], x2[i], y2[i]), orig_shape, trans_params
        )
        if ox2 - ox1 < 2 or oy2 - oy1 < 2:
            continue
        results.append((ox1, oy1, ox2, oy2, float(scores[i]), int(class_ids[i])))
    return results
