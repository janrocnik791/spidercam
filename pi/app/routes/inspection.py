"""
SpiderCam Pi – Inspection Routes (autonomous scan control)

    POST /api/inspection/start
    POST /api/inspection/pause
    POST /api/inspection/resume
    POST /api/inspection/stop
    POST /api/inspection/estop      also hard-halts the ESP32
    GET  /api/inspection/status

The full autonomous scan loop / path planner is out of scope here: start /
pause / resume / stop just drive the in-memory ScanState machine so the UI
buttons behave correctly. E-stop additionally commands the ESP32 to brake.
"""

from flask import Blueprint, jsonify

from app import socketio
from app.services import runtime
from app.services.esp_client import ESP32Unreachable

inspection_bp = Blueprint("inspection", __name__)


@inspection_bp.route("/start", methods=["POST"])
def start():
    return jsonify(runtime.scan.start())


@inspection_bp.route("/pause", methods=["POST"])
def pause():
    return jsonify(runtime.scan.pause())


@inspection_bp.route("/resume", methods=["POST"])
def resume():
    return jsonify(runtime.scan.resume())


@inspection_bp.route("/stop", methods=["POST"])
def stop():
    snap = runtime.scan.stop()
    socketio.emit("scan_complete", {})
    return jsonify(snap)


@inspection_bp.route("/estop", methods=["POST"])
def estop():
    snap = runtime.scan.estop()
    esp_ok = True
    try:
        runtime.esp.estop()
    except ESP32Unreachable:
        esp_ok = False
    socketio.emit("estop_triggered", {})
    return jsonify({**snap, "esp32": esp_ok})


@inspection_bp.route("/release", methods=["POST"])
def release():
    # Release E-stop → PAUSED (autonomous does not auto-resume; §7).
    return jsonify(runtime.scan.release_estop())


@inspection_bp.route("/status", methods=["GET"])
def status():
    return jsonify(runtime.scan.snapshot())
