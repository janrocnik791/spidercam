"""
SpiderCam Pi – Inspection Runner

Orchestrates one autonomous inspection pass:

  1. Reset the leak detector's per-pass zone memory.
  2. Capture loop @ 1 fps: grab the Y8 thermal frame → read the gantry position
     from the ESP32 → save the frame as data/inspections/{ts}/{x}_{y}.jpg →
     compare it live against the current baseline → emit any alerts over SocketIO.
  3. Stop when the ESP32 reports state == "idle" (returned home) or stop() is
     called externally.
  4. On completion, hand the whole pass to the comparison/persistence callback,
     then repoint the data/baselines/latest symlink at this pass so the next
     pass compares against it.

The autonomous path planner is out of scope (see services/runtime.py); this
runner captures at whatever position the controller reports and tolerates the
ESP32 being offline, which is the normal bring-up case.
"""

import logging
import os
import threading
import time
from datetime import datetime

import cv2

from app import config, socketio
from app.detection import leak_detector
from app.detection.comparator import compare_frames

log = logging.getLogger(__name__)


class InspectionRunner:
    def __init__(self, camera, esp_client, on_complete=None):
        """``on_complete(pass_dir, baseline_dir)`` runs once the pass finishes,
        before the baseline symlink is repointed, so it can diff this pass
        against the *previous* baseline (see routes/inspection.py)."""
        self.camera = camera
        self.esp = esp_client
        self.on_complete = on_complete
        self.timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.pass_dir = os.path.join(config.INSPECTIONS_DIR, self.timestamp)
        os.makedirs(self.pass_dir, exist_ok=True)
        self.running = False
        self._thread = None

    # ── lifecycle ────────────────────────────────────────────────────────────────
    def start(self) -> str:
        # Fresh zone memory each pass so a leak from a previous pass can alert
        # again (Argus Spec §5 treats each pass's detections independently).
        leak_detector.reset_alert_zones()
        self.running = True
        self._thread = threading.Thread(
            target=self._capture_loop, name=f"inspection-{self.timestamp}", daemon=True)
        self._thread.start()
        log.info("inspection pass %s started → %s", self.timestamp, self.pass_dir)
        return self.timestamp

    def stop(self):
        self.running = False
        try:
            self.esp.stop()
        except Exception as exc:           # ESP32 is frequently offline at bring-up
            log.debug("esp stop ignored: %s", exc)

    # ── capture ──────────────────────────────────────────────────────────────────
    def _capture_loop(self):
        try:
            while self.running:
                frame = self.camera.get_thermal_y8() if self.camera else None
                if frame is None:
                    time.sleep(1)
                    continue

                x, y = self._current_position()
                current_path = os.path.join(self.pass_dir, f"{x}_{y}.jpg")

                ok, buf = cv2.imencode(".jpg", frame)
                if ok:
                    with open(current_path, "wb") as f:
                        f.write(buf.tobytes())

                # Live comparison against the current baseline for this cell, so
                # the operator gets immediate alerts during the pass. The
                # authoritative batch comparison + persistence happens in
                # on_complete (routes/inspection.py).
                baseline_path = os.path.join(config.BASELINE_LATEST, f"{x}_{y}.jpg")
                try:
                    for a in compare_frames(baseline_path, current_path, x, y):
                        socketio.emit("inspection_alert", {
                            "x": a.x, "y": a.y,
                            "max_delta": a.max_delta, "bbox": list(a.bbox),
                        })
                except Exception:
                    log.exception("live comparison failed at (%s, %s)", x, y)

                if self._esp_idle():        # returned home → pass complete
                    self.running = False
                    break

                time.sleep(1)
        finally:
            self._on_pass_complete()

    def _current_position(self):
        """(x, y) in mm from the ESP32, falling back to the last known scan
        position when the controller is unreachable."""
        try:
            pos = self.esp.get_position()
            if isinstance(pos, dict) and "x" in pos and "y" in pos:
                return int(round(pos["x"])), int(round(pos["y"]))
        except Exception as exc:
            log.debug("esp position unavailable: %s", exc)
        from app.services import runtime
        p = runtime.scan.position
        return int(p.get("x", 0)), int(p.get("y", 0))

    def _esp_idle(self) -> bool:
        try:
            status = self.esp.get_status()
        except Exception:
            return False                    # offline → rely on external stop()
        return isinstance(status, dict) and status.get("state") == "idle"

    # ── completion ───────────────────────────────────────────────────────────────
    def _on_pass_complete(self):
        # Resolve the baseline BEFORE repointing it so the comparison diffs this
        # pass against the previous baseline, not against itself.
        baseline_dir = (config.BASELINE_LATEST
                        if os.path.exists(config.BASELINE_LATEST) else None)
        if self.on_complete is not None:
            try:
                self.on_complete(self.pass_dir, baseline_dir)
            except Exception:
                log.exception("inspection on_complete callback failed")
        self._update_baseline(self.pass_dir)
        log.info("inspection pass %s complete", self.timestamp)

    @staticmethod
    def _update_baseline(pass_dir):
        """Point data/baselines/latest at the freshly captured pass."""
        link = config.BASELINE_LATEST
        os.makedirs(os.path.dirname(link), exist_ok=True)
        try:
            if os.path.islink(link) or os.path.exists(link):
                os.remove(link)
            os.symlink(os.path.abspath(pass_dir), link)
        except OSError:
            log.exception("could not update baseline symlink %s", link)
