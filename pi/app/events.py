"""
SpiderCam Pi – SocketIO events (Argus Spec §8)

Browser → Pi commands and Pi → browser pushes, plus the background push
threads. Wired up by :func:`init` from create_app().

Two background tasks keep transport latency away from the live feed:

  * ``_push_loop``  – pure in-memory, never blocks: position_update (10 Hz),
    frame_stats (~8 Hz), scan_progress (1 Hz), and advancing the scan sim.
  * ``_esp_loop``   – may block on HTTP: polls the ESP32 every ~2 s and emits
    status_update; updates the head position when the controller is reachable.
"""

import logging

from app import socketio, config
from app.services import runtime
from app.services.esp_client import ESP32Unreachable

log = logging.getLogger(__name__)

_STATUS = {"ok": "ok", "warn": "warn", "danger": "danger"}


# ── Browser → Pi ────────────────────────────────────────────────────────────────
@socketio.on("connect")
def _on_connect():
    # Prime the client with current state so readouts aren't blank on load.
    socketio.emit("scan_progress", _scan_progress_payload())


@socketio.on("move")
def _on_move(data):
    data = data or {}
    direction = data.get("direction")
    speed = data.get("speed", runtime.scan.speed)
    if speed is not None:
        runtime.scan.set_speed(speed)
    if runtime.scan.estopped:
        return                      # motion ignored until E-stop is released
    try:
        runtime.esp.send_command(direction, speed=runtime.scan.speed)
    except (ESP32Unreachable, ValueError) as exc:
        log.debug("move ignored: %s", exc)


@socketio.on("stop_move")
def _on_stop_move(_data=None):
    try:
        runtime.esp.stop()
    except ESP32Unreachable:
        pass


@socketio.on("goto_home")
def _on_goto_home(_data=None):
    if runtime.scan.estopped:
        return
    try:
        runtime.esp.goto_home()
    except ESP32Unreachable:
        pass


@socketio.on("set_speed")
def _on_set_speed(data):
    runtime.scan.set_speed((data or {}).get("speed", runtime.scan.speed))


@socketio.on("start_scan")
def _on_start_scan(_data=None):
    # START INSPECTION button. Mirrors POST /api/inspection/start: flip the state
    # machine to autonomous and spin up the capture runner, then push the new
    # state so the UI tracks the active scan (idle → AUTONOMOUS) immediately.
    from app.routes.inspection import _start_runner   # lazy: avoid import cycle
    snap = runtime.scan.start()
    _start_runner()
    runtime.esp.start_inspection()       # fire-and-forget: tell the controller to scan
    socketio.emit("scan_progress", _scan_progress_payload(snap))


@socketio.on("pause_scan")
def _on_pause_scan(_data=None):
    snap = runtime.scan.pause()
    runtime.esp.pause()                  # fire-and-forget: pause the controller too
    socketio.emit("scan_progress", _scan_progress_payload(snap))


@socketio.on("resume_scan")
def _on_resume_scan(_data=None):
    snap = runtime.scan.resume()
    runtime.esp.resume()                 # fire-and-forget
    socketio.emit("scan_progress", _scan_progress_payload(snap))


@socketio.on("stop_scan")
def _on_stop_scan(_data=None):
    # The inspection panel's stop button emits this. Halt the pass on the Pi AND
    # tell the controller to abort its motion — without this the ESP32 never
    # hears the stop (the bug: stop_scan used to update only Pi-side state).
    from app.routes.inspection import _stop_runner   # lazy: avoid import cycle
    runtime.scan.stop()
    _stop_runner()                       # end the pass → batch compare + persist
    runtime.esp.abort()                  # fire-and-forget: stop controller motion
    socketio.emit("scan_complete", {})
    socketio.emit("scan_progress", _scan_progress_payload())


@socketio.on("abort_scan")
def _on_abort_scan(_data=None):
    # Abort cancels the current pass: halt capture, drop the scan back to idle,
    # and tell the controller to abort. Fire-and-forget on the ESP32 side.
    from app.routes.inspection import _stop_runner   # lazy: avoid import cycle
    snap = runtime.scan.stop()
    _stop_runner()
    runtime.esp.abort()                  # fire-and-forget
    socketio.emit("scan_complete", {})
    socketio.emit("scan_progress", _scan_progress_payload(snap))


@socketio.on("estop")
def _on_estop(_data=None):
    from app.routes.inspection import _stop_runner   # lazy: avoid import cycle
    runtime.scan.estop()
    _stop_runner()                       # halt the capture loop on abort
    runtime.esp.estop()                  # fire-and-forget: hard-halt the controller
    socketio.emit("estop_triggered", {})
    socketio.emit("scan_progress", _scan_progress_payload())


@socketio.on("release_estop")
def _on_release_estop(_data=None):
    # E-stop released → PAUSED. Per Argus Spec §7 autonomous must NOT
    # auto-resume; the operator presses RESUME afterwards.
    runtime.scan.release_estop()
    runtime.esp.release()                # fire-and-forget: clear the controller's latch
    socketio.emit("scan_progress", _scan_progress_payload())


@socketio.on("acknowledge_detection")
def _on_acknowledge(data):
    data = data or {}
    det_id = data.get("id")
    action = data.get("action", "acknowledge")
    status = {
        "acknowledge": "acknowledged",
        "resolve": "resolved",
        "false_positive": "false_positive",
    }.get(action, "acknowledged")
    det = runtime.detections.update_status(det_id, status)
    if det is not None:
        socketio.emit("detection_updated", {"id": det_id, "status": status})


# ── payload helpers ───────────────────────────────────────────────────────────
def _scan_progress_payload(snap=None):
    s = snap or runtime.scan.snapshot()
    return {
        "mode": s["mode"],
        "progress": s["progress"],
        "elapsed_seconds": s["elapsed_seconds"],
        "eta_seconds": s["eta_seconds"],
        "current_pass": s["current_pass"],
        "total_passes": s["total_passes"],
        "current_cell": s["current_cell"],
        "total_cells": s["total_cells"],
    }


def _build_status(esp_reachable, esp_status):
    scan = runtime.scan
    cam = runtime.camera
    fps = cam.thermal_fps() if cam else 0.0
    streaming = bool(cam and cam.get_thermal_stats() is not None)

    # ESP32 motor controller link.
    esp32 = ({"state": "ok", "detail": config.ESP32_BASE_URL.replace("http://", "")}
             if esp_reachable
             else {"state": "danger", "detail": "controller offline"})

    # Raspberry Pi camera stream + server.
    pi = ({"state": "ok", "detail": f"{fps:.1f} fps stream"}
          if streaming
          else {"state": "warn", "detail": "stream starting…"})

    # All 4 cable motors.
    if scan.estopped:
        motors = {"state": "danger", "detail": "E-STOPPED · brakes held"}
    elif not esp_reachable:
        motors = {"state": "danger", "detail": "controller offline"}
    else:
        current = esp_status.get("current") if isinstance(esp_status, dict) else None
        detail = f"4 cables · {current} A" if current is not None else "4 cables"
        motors = {"state": "ok", "detail": detail}

    # Thermal sensor health.
    if streaming:
        stats = cam.get_thermal_stats()
        ambient = stats["min"] if stats else None
        ir = {"state": "ok",
              "detail": f"{ambient:.1f}°C ambient" if ambient is not None else "online"}
    else:
        ir = {"state": "warn", "detail": "calibrating"}

    return {"esp32": esp32, "pi": pi, "motors": motors, "ir_sensor": ir}


# ── Background push tasks ───────────────────────────────────────────────────────
def _push_loop():
    tick = 0.05                                  # 20 Hz base tick
    pos_period = 1.0 / config.PUSH_POSITION_HZ
    frame_period = 1.0 / config.PUSH_FRAME_STATS_HZ
    scan_period = 1.0 / config.PUSH_SCAN_PROGRESS_HZ
    t_pos = t_frame = t_scan = 0.0
    while True:
        snap = runtime.scan.tick()
        t_pos += tick
        t_frame += tick
        t_scan += tick

        if t_pos >= pos_period:
            t_pos = 0.0
            socketio.emit("position_update", snap["position"])

        if t_frame >= frame_period and runtime.camera is not None:
            t_frame = 0.0
            stats = runtime.camera.get_thermal_stats()
            if stats is not None:
                socketio.emit("frame_stats", stats)

        if t_scan >= scan_period:
            t_scan = 0.0
            socketio.emit("scan_progress", _scan_progress_payload(snap))

        socketio.sleep(tick)


def _esp_loop():
    while True:
        reachable = False
        esp_status = {}
        try:
            reachable = runtime.esp.ping()
            if reachable:
                pos = runtime.esp.get_position()
                if isinstance(pos, dict) and {"x", "y", "z"} <= set(pos):
                    runtime.scan.set_position(pos["x"], pos["y"], pos["z"])
                esp_status = runtime.esp.get_status()
        except ESP32Unreachable:
            reachable = False
        if not reachable:
            runtime.scan.clear_esp_position()
        socketio.emit("status_update", _build_status(reachable, esp_status))
        socketio.sleep(config.PUSH_STATUS_PERIOD_S)


def emit_new_detection(detection):
    """Add a detection to the store and push it to clients (pipeline entry point)."""
    runtime.detections.add(detection)
    socketio.emit("new_detection", detection)


def init():
    """Start the background push tasks. Handlers are registered at import time."""
    socketio.start_background_task(_push_loop)
    socketio.start_background_task(_esp_loop)
    log.info("Argus SocketIO push tasks started")
