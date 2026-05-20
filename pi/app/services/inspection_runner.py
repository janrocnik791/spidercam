"""
SpiderCam Pi – Inspection Runner

Orchestrates a full inspection pass:
  1. Tell ESP32 to start autonomous route
  2. Loop: capture frame + get position → save frame to disk
  3. Detect when ESP32 reports state=idle (returned home) → stop loop
  4. Hand off to comparator to run leak detection
  5. Update the baseline symlink if user confirms pass was clean

Claude Code instructions:

    class InspectionRunner:

        def __init__(self, camera, esp_client):
            Store camera and esp_client references.
            Create a timestamped directory under config.INSPECTIONS_DIR.
            Set self.running = False.

        def start(self) -> str:
            Send esp_client.start_inspection().
            Set self.running = True.
            Start a background thread running _capture_loop().
            Return the inspection timestamp/ID.

        def stop(self):
            Set self.running = False.
            Call esp_client.stop().

        def _capture_loop(self):
            While self.running:
                frame = camera.get_frame()
                x, y = esp_client.get_position()
                save frame as np.save(f"{pass_dir}/{x}_{y}.npy", frame)
                check esp_client.get_status() — if state == "idle" → self.running = False
                time.sleep(config.CAPTURE_INTERVAL_MS / 1000)
            _on_pass_complete()

        def _on_pass_complete(self):
            Run comparator.compare_pass(current_pass_dir, config.BASELINE_LATEST).
            Emit results via SocketIO so the browser updates in real time.

Frame naming: "{x}_{y}.npy"
  Multiple frames at the same coordinate (gantry pauses) → average them before saving.
  This is the noise reduction at capture time (different from the comparison-time filter).
"""

# import os, time, threading
# import numpy as np
# from datetime import datetime
# from app import socketio
# from app import config
# from app.detection.comparator import compare_pass
