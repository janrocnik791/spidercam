"""
SpiderCam Pi – Results Routes (detection records)

    GET    /api/results/detections
    GET    /api/results/detections/<id>
    PATCH  /api/results/detections/<id>            {"status": ...}
    GET    /api/results/detections/<id>/frame/<type>   type ∈ past|current|difference

Detections live in the in-memory DetectionStore (Argus Spec §5 schema). The
list is empty until the detection pipeline emits records via SocketIO. Saved
frame files don't exist yet, so the frame endpoint returns 404 for now.
"""

import os

from flask import Blueprint, jsonify, request, send_file

from app import socketio, config
from app.services import runtime

results_bp = Blueprint("results", __name__)

_VALID_STATUS = {"new", "acknowledged", "resolved", "false_positive"}
_VALID_FRAME = {"past", "current", "difference"}


@results_bp.route("/detections", methods=["GET"])
def list_detections():
    return jsonify(runtime.detections.all())


@results_bp.route("/detections/<det_id>", methods=["GET"])
def get_detection(det_id):
    det = runtime.detections.get(det_id)
    if det is None:
        return jsonify({"error": "not found"}), 404
    return jsonify(det)


@results_bp.route("/detections/<det_id>", methods=["PATCH"])
def patch_detection(det_id):
    body = request.get_json(silent=True) or {}
    status = body.get("status")
    if status not in _VALID_STATUS:
        return jsonify({"error": f"invalid status: {status!r}"}), 400
    det = runtime.detections.update_status(det_id, status)
    if det is None:
        return jsonify({"error": "not found"}), 404
    socketio.emit("detection_updated", {"id": det_id, "status": status})
    return jsonify(det)


@results_bp.route("/detections/<det_id>/frame/<frame_type>", methods=["GET"])
def get_frame(det_id, frame_type):
    if frame_type not in _VALID_FRAME:
        return jsonify({"error": f"invalid frame type: {frame_type!r}"}), 400
    # Saved frames will live under data/inspections/<...>; none exist yet.
    path = os.path.join(config.INSPECTIONS_DIR, det_id, f"{frame_type}.jpg")
    if not os.path.isfile(path):
        return jsonify({"error": "frame not available"}), 404
    return send_file(path, mimetype="image/jpeg")
