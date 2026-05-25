"""
SpiderCam Pi – Shared runtime state for the Argus backend.

One source of truth that every route and SocketIO handler reads through:

  * ``camera``      – the FLIR camera service (set once at startup)
  * ``esp``         – the ESP32 HTTP client (set once at startup)
  * ``scan``        – ScanState: autonomous-scan + system state machine
  * ``detections``  – DetectionStore: in-memory detection records

The autonomous scan loop / path planner is out of scope for this backend, so
ScanState advances a *simulated* progress/position when running. That keeps the
UI alive and lets the control buttons behave correctly, while leaving a clean
seam for the real path planner to drive ``scan`` instead.
"""

import threading
import time

from app import config

# ── Module-level singletons, populated by create_app() ──────────────────────────
camera = None        # app.services.camera_flir.FLIROneProCamera
esp = None           # app.services.esp_client.ESP32Client
runner = None        # app.services.inspection_runner.InspectionRunner (current pass)


class ScanState:
    """Autonomous-scan + system state machine. All access is lock-guarded."""

    # mode ∈ {autonomous, paused, manual, idle, estop, complete}
    def __init__(self):
        self._lock = threading.RLock()
        # Boot idle: no scan runs (and tick() advances nothing) until the
        # operator calls POST /api/inspection/start. Booting into "autonomous"
        # made the server auto-start a (simulated) pass on every boot.
        self.mode = "idle"
        self.estopped = False
        self.speed = config.DEFAULT_SPEED
        self.progress = 0.0
        self.total_passes = config.SCAN_TOTAL_PASSES
        self.total_cells = config.SCAN_TOTAL_CELLS
        self.position = {"x": 0, "y": 0, "z": config.Z_MAX_MM // 2}
        self._position_from_esp = False
        self._last_tick = time.monotonic()

    # ── state transitions ───────────────────────────────────────────────────────
    def start(self):
        with self._lock:
            self.estopped = False
            self.mode = "autonomous"
            self.progress = 0.0
            self._last_tick = time.monotonic()
            return self.snapshot()

    def pause(self):
        with self._lock:
            if not self.estopped and self.mode == "autonomous":
                self.mode = "paused"
            return self.snapshot()

    def resume(self):
        with self._lock:
            # RESUME only un-pauses a scan that is genuinely mid-pass. A finished
            # (complete) or never-started (idle) scan must NOT be revived here:
            # resume() does not spin up an InspectionRunner, so flipping such a
            # scan back to "autonomous" would advance the progress sim with no
            # capture attached — a phantom pass that records zero frames. Begin a
            # fresh pass through start() + _start_runner() (the START INSPECTION
            # button) instead, which starts the sim and the runner together.
            if not self.estopped and self.mode == "paused":
                self.mode = "autonomous"
                self._last_tick = time.monotonic()
            return self.snapshot()

    def stop(self):
        with self._lock:
            if not self.estopped:
                self.mode = "idle"
            return self.snapshot()

    def set_manual(self):
        """Operator switched to the MANUAL tab — autonomous always pauses."""
        with self._lock:
            if not self.estopped:
                self.mode = "manual"
            return self.snapshot()

    def set_inspection(self):
        """Operator switched back to INSPECTION — resume autonomous."""
        return self.resume()

    def estop(self):
        with self._lock:
            self.estopped = True
            self.mode = "estop"
            return self.snapshot()

    def release_estop(self):
        with self._lock:
            self.estopped = False
            self.mode = "paused"        # never auto-resume after an E-stop
            return self.snapshot()

    def set_speed(self, value):
        with self._lock:
            self.speed = max(0, min(100, int(value)))
            return self.speed

    def set_position(self, x, y, z):
        with self._lock:
            self.position = {"x": int(x), "y": int(y), "z": int(z)}
            self._position_from_esp = True

    def clear_esp_position(self):
        """ESP32 went away — let the simulated position take over again."""
        with self._lock:
            self._position_from_esp = False

    # ── time advance (called by the background push thread) ──────────────────────
    def tick(self):
        """Advance simulated progress/position while running. Returns snapshot."""
        with self._lock:
            now = time.monotonic()
            dt = now - self._last_tick
            self._last_tick = now
            if self.mode == "autonomous" and not self.estopped:
                self.progress = min(1.0, self.progress + dt / config.SCAN_DURATION_S)
                if not self._position_from_esp:
                    self.position = self._sim_position(self.progress)
                if self.progress >= 1.0:
                    self.mode = "complete"
            return self.snapshot()

    def _sim_position(self, progress):
        """Place the head along the serpentine path for the given progress."""
        passes = self.total_passes
        head = progress * passes
        row = min(passes - 1, int(head))
        frac = head - row
        going_right = (row % 2 == 0)
        x_norm = frac if going_right else (1.0 - frac)
        y_norm = (row + 0.5) / passes
        return {
            "x": round(x_norm * config.PLANT_W_MM),
            "y": round(y_norm * config.PLANT_H_MM),
            "z": config.Z_MAX_MM // 2,
        }

    def cell_center(self, cell_index):
        """Deterministic (x, y) in mm for a 1-based serpentine cell index.

        Used to name captured frames by cell so the same cell maps to the same
        filename across passes (making the pass-to-pass diff meaningful). Mirrors
        the sweep direction of ``_sim_position`` and is purely time-derived — no
        ESP position involved. To use the real gantry later, feed its (x, y) into
        the same filename instead."""
        cells_per_pass = max(1, self.total_cells // self.total_passes)
        c = max(0, min(self.total_cells - 1, int(cell_index) - 1))
        row = c // cells_per_pass
        col = c % cells_per_pass
        col_eff = col if (row % 2 == 0) else (cells_per_pass - 1 - col)
        x = round((col_eff + 0.5) / cells_per_pass * config.PLANT_W_MM)
        y = round((row + 0.5) / self.total_passes * config.PLANT_H_MM)
        return x, y

    # ── snapshots ────────────────────────────────────────────────────────────────
    @property
    def running(self):
        return self.mode == "autonomous"

    def snapshot(self):
        with self._lock:
            elapsed = round(self.progress * config.SCAN_DURATION_S)
            eta = round((1.0 - self.progress) * config.SCAN_DURATION_S)
            cells_done = int(round(self.progress * self.total_cells))
            current_cell = min(self.total_cells, cells_done + 1)
            cells_per_pass = max(1, self.total_cells // self.total_passes)
            current_pass = min(self.total_passes, cells_done // cells_per_pass + 1)
            return {
                "mode": self.mode,
                "estopped": self.estopped,
                "progress": round(self.progress, 4),
                "elapsed_seconds": elapsed,
                "eta_seconds": eta,
                "current_pass": current_pass,
                "total_passes": self.total_passes,
                "current_cell": current_cell,
                "total_cells": self.total_cells,
                "speed": self.speed,
                "position": dict(self.position),
            }


class DetectionStore:
    """Thread-safe in-memory list of detection records (Argus Spec §5 schema).

    Newest first. Persisting to disk/SQLite is a future step; the API shape is
    already the contract the UI consumes, so swapping the backing store later
    won't touch callers.
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._items = []

    def all(self):
        with self._lock:
            return [dict(d) for d in self._items]

    def get(self, det_id):
        with self._lock:
            for d in self._items:
                if d["id"] == det_id:
                    return dict(d)
        return None

    def add(self, detection):
        with self._lock:
            self._items.insert(0, dict(detection))
        return dict(detection)

    def update_status(self, det_id, status):
        with self._lock:
            for d in self._items:
                if d["id"] == det_id:
                    d["status"] = status
                    return dict(d)
        return None

    def count(self):
        with self._lock:
            return len(self._items)


# Shared instances (state is process-global, not per-request).
scan = ScanState()
detections = DetectionStore()
