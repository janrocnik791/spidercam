"""
SpiderCam Pi – Control Routes (manual motion)

    POST /api/move        {"direction": left|right|forward|backward|up|down}
    POST /api/stop
    POST /api/goto_home
    POST /api/speed       {"speed": 0..100}
    GET  /api/ping        {"ok": true|false}

Motor speed is session state (set via /api/speed) and is attached to every
move command. The ESP32 may be offline — those endpoints return 503 with a
clear payload rather than crashing.
"""

from flask import Blueprint, jsonify, request

from app.services import runtime
from app.services.esp_client import ESP32Unreachable

control_bp = Blueprint("control", __name__)

_VALID_DIRECTIONS = {"left", "right", "forward", "backward", "up", "down"}


@control_bp.route("/move", methods=["POST"])
def move():
    body = request.get_json(silent=True) or {}
    direction = body.get("direction")
    if direction not in _VALID_DIRECTIONS:
        return jsonify({"error": f"invalid direction: {direction!r}"}), 400
    speed = body.get("speed", runtime.scan.speed)
    try:
        result = runtime.esp.send_command(direction, speed=speed)
    except ESP32Unreachable as exc:
        return jsonify({"ok": False, "error": "esp32 unreachable", "detail": str(exc)}), 503
    return jsonify(result)


@control_bp.route("/stop", methods=["POST"])
def stop():
    try:
        result = runtime.esp.stop()
    except ESP32Unreachable as exc:
        return jsonify({"ok": False, "error": "esp32 unreachable", "detail": str(exc)}), 503
    return jsonify(result or {"ok": True})


@control_bp.route("/goto_home", methods=["POST"])
def goto_home():
    try:
        result = runtime.esp.goto_home()
    except ESP32Unreachable as exc:
        return jsonify({"ok": False, "error": "esp32 unreachable", "detail": str(exc)}), 503
    return jsonify(result or {"ok": True})


@control_bp.route("/speed", methods=["POST"])
def set_speed():
    body = request.get_json(silent=True) or {}
    raw = body.get("speed")
    try:
        value = int(raw)
    except (TypeError, ValueError):
        return jsonify({"error": "speed must be an integer 0–100"}), 400
    if not 0 <= value <= 100:
        return jsonify({"error": "speed must be 0–100"}), 400
    runtime.scan.set_speed(value)
    return jsonify({"ok": True, "speed": value})


@control_bp.route("/ping", methods=["GET"])
def ping():
    return jsonify({"ok": runtime.esp.ping()})
