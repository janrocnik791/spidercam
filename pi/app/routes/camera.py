"""
SpiderCam Pi – Camera Routes

MJPEG streams + a JSON stats fallback, served from the FLIR camera service.

    GET /api/camera/thermal        multipart MJPEG, iron-colorized thermal
    GET /api/camera/optical        multipart MJPEG, visible-light camera
    GET /api/camera/thermal/stats  {"min","avg","max"} in °C (SocketIO is primary)

The hero <img> in the Argus UI points straight at /thermal and /optical.
"""

import time

import cv2
from flask import Blueprint, Response, jsonify

from app import config
from app.services import runtime

camera_bp = Blueprint("camera", __name__)

_STREAM_DELAY = 1.0 / 15.0   # cap each MJPEG stream at ~15 fps
_ENCODE = [cv2.IMWRITE_JPEG_QUALITY, config.FLIR_JPEG_QUALITY]


def _mjpeg(grab):
    """Yield multipart/x-mixed-replace frames from ``grab()`` (returns BGR or None)."""
    while True:
        frame = grab()
        if frame is None:
            time.sleep(0.05)
            continue
        ok, buf = cv2.imencode(".jpg", frame, _ENCODE)
        if not ok:
            time.sleep(0.05)
            continue
        jpeg = buf.tobytes()
        yield (b"--frame\r\n"
               b"Content-Type: image/jpeg\r\n"
               b"Content-Length: " + str(len(jpeg)).encode() + b"\r\n\r\n"
               + jpeg + b"\r\n")
        time.sleep(_STREAM_DELAY)


def _thermal_only():
    if runtime.camera is None:
        return None
    frame, _ = runtime.camera.get_thermal_frame()
    return frame


@camera_bp.route("/thermal")
def thermal_stream():
    return Response(_mjpeg(_thermal_only),
                    mimetype="multipart/x-mixed-replace; boundary=frame")


@camera_bp.route("/optical")
def optical_stream():
    grab = (lambda: runtime.camera.get_visual_frame()) if runtime.camera else (lambda: None)
    return Response(_mjpeg(grab),
                    mimetype="multipart/x-mixed-replace; boundary=frame")


@camera_bp.route("/thermal/stats")
def thermal_stats():
    if runtime.camera is None:
        return jsonify({"min": None, "avg": None, "max": None}), 503
    stats = runtime.camera.get_thermal_stats()
    if stats is None:
        return jsonify({"min": None, "avg": None, "max": None}), 503
    return jsonify(stats)
