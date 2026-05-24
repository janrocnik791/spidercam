"""
SpiderCam Pi – Inspection Routes (autonomous scan control)

    POST /api/inspection/start
    POST /api/inspection/pause
    POST /api/inspection/resume
    POST /api/inspection/stop
    POST /api/inspection/estop      also hard-halts the ESP32
    GET  /api/inspection/status

start / pause / resume / stop drive the in-memory ScanState machine so the UI
buttons behave correctly. start additionally kicks off an InspectionRunner that
captures thermal frames for the pass; when the pass finishes the runner hands
the captured frames to _publish_pass_results(), which runs the leak comparison,
stores each LeakAlert in the DetectionStore, and pushes it to the browser.
E-stop additionally commands the ESP32 to brake and halts the capture.
"""

from datetime import datetime

from flask import Blueprint, jsonify

from app import socketio, config
from app.services import runtime
from app.services.esp_client import ESP32Unreachable
from app.services.inspection_runner import InspectionRunner
from app.detection import leak_detector
from app.detection.comparator import compare_pass

inspection_bp = Blueprint("inspection", __name__)


@inspection_bp.route("/start", methods=["POST"])
def start():
    snap = runtime.scan.start()
    _start_runner()
    return jsonify(snap)


@inspection_bp.route("/pause", methods=["POST"])
def pause():
    return jsonify(runtime.scan.pause())


@inspection_bp.route("/resume", methods=["POST"])
def resume():
    return jsonify(runtime.scan.resume())


@inspection_bp.route("/stop", methods=["POST"])
def stop():
    snap = runtime.scan.stop()
    _stop_runner()
    socketio.emit("scan_complete", {})
    return jsonify(snap)


@inspection_bp.route("/estop", methods=["POST"])
def estop():
    snap = runtime.scan.estop()
    _stop_runner()
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


# ── inspection runner wiring ────────────────────────────────────────────────────
def _start_runner():
    """Begin capturing frames for a new pass (replacing any running pass)."""
    if runtime.runner is not None and runtime.runner.running:
        runtime.runner.stop()
    runtime.runner = InspectionRunner(
        runtime.camera, runtime.esp, on_complete=_publish_pass_results)
    runtime.runner.start()


def _stop_runner():
    if runtime.runner is not None:
        runtime.runner.stop()


def _publish_pass_results(current_dir, baseline_dir):
    """Runner completion hook: run the authoritative comparison over the whole
    pass, store each LeakAlert in the DetectionStore, and push it to the UI.

    The live capture loop already populated the per-pass zone memory, so reset
    it here — otherwise the batch comparison would suppress everything the live
    phase already saw and store nothing.
    """
    from app import events   # lazy: events registers SocketIO handlers at import

    leak_detector.reset_alert_zones()
    alerts = compare_pass(current_dir, baseline_dir)
    for idx, alert in enumerate(alerts, start=1):
        detection = _alert_to_detection(alert, idx)
        events.emit_new_detection(detection)   # add to store + emit "new_detection"


def _alert_to_detection(alert, idx):
    """Map a LeakAlert onto a detection record (Argus Spec §5-ish).

    The Y8 loopback feed carries no calibrated temperatures, so the record
    exposes the raw Y8 diff delta (0–255) and the gantry coordinates rather than
    fabricating °C values; priority scales with the flagged region's size.
    """
    bx, by, bw, bh = alert.bbox
    area = bw * bh
    priority = ("critical" if area >= 2000 else
                "high" if area >= 800 else
                "medium" if area >= 200 else "low")
    now = datetime.now()
    loc_x = max(0.0, min(1.0, alert.x / config.PLANT_W_MM))
    loc_y = max(0.0, min(1.0, alert.y / config.PLANT_H_MM))
    z = runtime.scan.position.get("z", config.Z_MAX_MM // 2)
    return {
        "id": f"L-{config.SESSION_ID:03d}-{idx:03d}",
        "priority": priority,
        "status": "new",
        "pass": f"{idx:03d}",
        "time": now.strftime("%H:%M"),
        "date": now.strftime("%Y-%m-%d"),
        "max_delta": round(alert.max_delta, 1),     # Y8 0–255 intensity delta
        "bbox": [bx, by, bw, bh],
        "location": {"x": alert.x, "y": alert.y, "z": z},
        "locNorm": {"x": round(loc_x, 4), "y": round(loc_y, 4)},
        "mission": config.MISSION_NAME,
        "notes": "",
    }
