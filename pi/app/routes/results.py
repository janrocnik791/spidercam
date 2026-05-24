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

from app import socketio
from app.services import runtime

results_bp = Blueprint("results", __name__)

_VALID_STATUS = {"new", "acknowledged", "resolved", "false_positive"}
_VALID_FRAME = {"past", "current", "difference"}


def _public(det):
    """Detection record for JSON responses: keep the ``frames`` URLs but drop the
    internal on-disk ``frame_files`` paths."""
    d = dict(det)
    d.pop("frame_files", None)
    return d


@results_bp.route("/detections", methods=["GET"])
def list_detections():
    return jsonify([_public(d) for d in runtime.detections.all()])


@results_bp.route("/detections/<det_id>", methods=["GET"])
def get_detection(det_id):
    det = runtime.detections.get(det_id)
    if det is None:
        return jsonify({"error": "not found"}), 404
    return jsonify(_public(det))


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
    return jsonify(_public(det))


@results_bp.route("/detections/<det_id>/frame/<frame_type>", methods=["GET"])
def get_frame(det_id, frame_type):
    if frame_type not in _VALID_FRAME:
        return jsonify({"error": f"invalid frame type: {frame_type!r}"}), 400
    det = runtime.detections.get(det_id)
    if det is None:
        return jsonify({"error": "not found"}), 404
    # The comparator saved these under data/inspections/<pass>/<x>_<y>_<kind>.jpg
    # and the detection record carries their resolved paths.
    path = (det.get("frame_files") or {}).get(frame_type)
    if not path or not os.path.isfile(path):
        return jsonify({"error": "frame not available"}), 404
    return send_file(path, mimetype="image/jpeg")
