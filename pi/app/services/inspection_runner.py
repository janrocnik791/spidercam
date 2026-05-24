"""
SpiderCam Pi – Inspection Runner (time-based)

Orchestrates one inspection pass, driven entirely by the simulated time-based
scan in services/runtime.py — no ESP / gantry position required:

  1. Reset the leak detector's per-pass zone memory.
  2. Capture loop: as the simulated sweep advances cell-by-cell (ScanState,
     derived from the SCAN_DURATION_S progress clock), grab the Y8 thermal frame
     for each new cell and save it as data/inspections/{ts}/{x}_{y}.jpg, where
     (x, y) is the cell's deterministic centre. Same cell → same filename across
     passes, so the pass-to-pass comparison lines up. Each frame is compared live
     against the current baseline and any alert is pushed over SocketIO.
  3. Finish when the scan reaches 100% (ScanState mode == "complete") or when
     stop() is called. PAUSED / E-STOP just idle the loop in place.
  4. On finish, hand the whole pass to the comparison/persistence callback, then
     copy this pass's raw frames into data/baselines/latest so the next pass
     compares against them.

Naming frames by the *simulated* cell (not the ESP) keeps everything on the
time-based clock; wiring the real gantry position back in later only means
feeding a real (x, y) into the same filename.
"""

import logging
import os
import shutil
import threading
import time
from datetime import datetime

import cv2

from app import config, socketio
from app.services import runtime
from app.detection import leak_detector
from app.detection.comparator import compare_frames

log = logging.getLogger(__name__)

# Suffixes of the derived overlay images the comparator writes into a pass dir.
# These are views for the history detail page, never reference frames, so they
# are excluded when building the baseline.
_OVERLAY_SUFFIXES = ("_baseline", "_diff", "_current")


class InspectionRunner:
    def __init__(self, camera, esp_client=None, on_complete=None):
        """``on_complete(pass_dir, baseline_dir)`` runs once the pass finishes,
        before the baseline is refreshed, so it can diff this pass against the
        *previous* baseline (see routes/inspection.py).

        ``esp_client`` is accepted for call-site compatibility but unused —
        capture is purely time-based off the simulated scan."""
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
        # Time-based runner: just end the capture loop (no ESP motion to halt).
        self.running = False

    # ── capture ──────────────────────────────────────────────────────────────────
    def _capture_loop(self):
        """One frame per simulated scan cell. Finishes at 100% or on stop()."""
        last_cell = None
        try:
            while self.running:
                mode = runtime.scan.mode
                if mode == "complete":
                    break                        # scan reached 100% → finalize
                if mode != "autonomous":
                    time.sleep(0.2)              # PAUSED / E-STOP / idle → hold
                    continue

                cell = runtime.scan.snapshot()["current_cell"]
                if cell == last_cell:
                    time.sleep(0.1)             # still on this cell — poll for next
                    continue

                frame = self.camera.get_thermal_y8() if self.camera else None
                if frame is None:
                    time.sleep(0.2)             # no frame yet — retry this cell
                    continue

                last_cell = cell
                x, y = runtime.scan.cell_center(cell)
                filename = f"{x}_{y}.jpg"
                current_path = os.path.join(self.pass_dir, filename)

                ok, buf = cv2.imencode(".jpg", frame)
                if ok:
                    with open(current_path, "wb") as f:
                        f.write(buf.tobytes())
                    self._write_temp_sidecar(current_path)

                # Live comparison against the current baseline for this cell, so
                # the operator gets immediate alerts during the pass. The
                # authoritative batch comparison + persistence happens in
                # on_complete (routes/inspection.py).
                baseline_path = os.path.join(config.BASELINE_LATEST, filename)
                try:
                    for a in compare_frames(baseline_path, current_path, x, y):
                        socketio.emit("inspection_alert", {
                            "x": a.x, "y": a.y,
                            "max_delta": a.max_delta, "bbox": list(a.bbox),
                        })
                except Exception:
                    log.exception("live comparison failed at (%s, %s)", x, y)
        finally:
            self.running = False
            self._on_pass_complete()

    def _write_temp_sidecar(self, frame_path):
        """Persist the frame's calibrated min/max °C next to it (``{stem}.temp``)
        so the comparator can reconstruct per-pixel temperature and apply the
        absolute hot gate — on both the live and end-of-pass batch paths."""
        try:
            stats = self.camera.get_thermal_stats() if self.camera else None
        except Exception:
            stats = None
        if not stats or "min" not in stats or "max" not in stats:
            return
        try:
            with open(os.path.splitext(frame_path)[0] + ".temp", "w") as f:
                f.write(f"{stats['min']} {stats['max']}")
        except OSError:
            pass

    # ── completion ───────────────────────────────────────────────────────────────
    def _on_pass_complete(self):
        # Resolve the baseline BEFORE refreshing it so the comparison diffs this
        # pass against the *previous* baseline, not against itself.
        baseline_dir = (config.BASELINE_LATEST
                        if os.path.isdir(config.BASELINE_LATEST) else None)
        if self.on_complete is not None:
            try:
                self.on_complete(self.pass_dir, baseline_dir)
            except Exception:
                log.exception("inspection on_complete callback failed")
        self._update_baseline(self.pass_dir)
        log.info("inspection pass %s complete", self.timestamp)

    @staticmethod
    def _update_baseline(pass_dir):
        """Replace data/baselines/latest with a clean copy of this pass's raw
        capture frames ({x}_{y}.jpg only — not the _diff/_baseline overlays), so
        the next pass compares against them. It is a real folder (not a symlink),
        so the baseline is self-contained and free of overlay clutter."""
        dest = config.BASELINE_LATEST
        try:
            if os.path.islink(dest) or os.path.isfile(dest):
                os.remove(dest)
            elif os.path.isdir(dest):
                shutil.rmtree(dest)
            os.makedirs(dest, exist_ok=True)
            for name in os.listdir(pass_dir):
                if not name.endswith(".jpg"):
                    continue
                if name[:-len(".jpg")].endswith(_OVERLAY_SUFFIXES):
                    continue                     # derived overlay — not a reference
                shutil.copy2(os.path.join(pass_dir, name), os.path.join(dest, name))
        except OSError:
            log.exception("could not update baseline %s", dest)
