"""
SpiderCam Pi – Camera Routes
"""

import time
import threading

import cv2
import numpy as np
from flask import Blueprint, Response, jsonify

from app.services.camera_base import get_camera

camera_bp = Blueprint("camera", __name__)
_camera = None
_cam_lock = threading.Lock()

_W, _H = 640, 480
_BAR_W = 40
_FONT = cv2.FONT_HERSHEY_SIMPLEX


def _get_camera():
    global _camera
    if _camera is None:
        _camera = get_camera()
    return _camera


def _render_frame(raw: np.ndarray) -> np.ndarray:
    """Return a composite BGR image: upscaled thermal + colorbar strip."""
    t_min, t_max = float(raw.min()), float(raw.max())

    # Upscale 32×24 → 640×480, nearest-neighbour keeps the block style
    norm = cv2.normalize(raw, None, 0, 255, cv2.NORM_MINMAX, cv2.CV_8U)
    norm_up = cv2.resize(norm, (_W, _H), interpolation=cv2.INTER_NEAREST)
    color = cv2.applyColorMap(norm_up, cv2.COLORMAP_INFERNO)

    # Min/max text overlay in top-left
    cv2.putText(color, f"min {t_min:.1f}C", (10, 26), _FONT, 0.65, (0, 0, 0), 2, cv2.LINE_AA)
    cv2.putText(color, f"min {t_min:.1f}C", (10, 26), _FONT, 0.65, (255, 255, 255), 1, cv2.LINE_AA)
    cv2.putText(color, f"max {t_max:.1f}C", (10, 52), _FONT, 0.65, (0, 0, 0), 2, cv2.LINE_AA)
    cv2.putText(color, f"max {t_max:.1f}C", (10, 52), _FONT, 0.65, (255, 255, 255), 1, cv2.LINE_AA)

    # Colorbar: 480×40 vertical gradient, hot at top
    grad = np.linspace(255, 0, _H, dtype=np.uint8).reshape(_H, 1)
    colorbar = cv2.applyColorMap(np.tile(grad, (1, _BAR_W)), cv2.COLORMAP_INFERNO)
    # Max label at top, min at bottom
    cv2.putText(colorbar, f"{t_max:.0f}", (3, 14), _FONT, 0.38, (0, 0, 0), 2, cv2.LINE_AA)
    cv2.putText(colorbar, f"{t_max:.0f}", (3, 14), _FONT, 0.38, (255, 255, 255), 1, cv2.LINE_AA)
    cv2.putText(colorbar, f"{t_min:.0f}", (3, _H - 5), _FONT, 0.38, (0, 0, 0), 2, cv2.LINE_AA)
    cv2.putText(colorbar, f"{t_min:.0f}", (3, _H - 5), _FONT, 0.38, (255, 255, 255), 1, cv2.LINE_AA)

    return np.concatenate([color, colorbar], axis=1)


@camera_bp.route("/frame")
def frame():
    cam = _get_camera()
    with _cam_lock:
        raw = cam.get_frame()
    img = _render_frame(raw)
    _, buf = cv2.imencode(".png", img)
    return Response(buf.tobytes(), mimetype="image/png")


@camera_bp.route("/frame.json")
def frame_json():
    cam = _get_camera()
    with _cam_lock:
        raw = cam.get_frame()
    h, w = raw.shape
    return jsonify({
        "temps": raw.tolist(),
        "min": float(raw.min()),
        "max": float(raw.max()),
        "width": w,
        "height": h,
    })


@camera_bp.route("/stream")
def stream():
    def generate():
        cam = _get_camera()
        while True:
            try:
                with _cam_lock:
                    raw = cam.get_frame()
                img = _render_frame(raw)
                _, buf = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 85])
                yield (
                    b"--frame\r\n"
                    b"Content-Type: image/jpeg\r\n\r\n"
                    + buf.tobytes()
                    + b"\r\n"
                )
            except Exception:
                time.sleep(0.25)

    return Response(
        generate(),
        mimetype="multipart/x-mixed-replace; boundary=frame",
    )
