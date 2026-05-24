"""
SpiderCam Pi – Central Configuration
All tuneable values live here. Never hardcode these elsewhere.
"""

import os

# ── ESP32 ─────────────────────────────────────────────────────────────────────
# Set this to the IP address printed by the ESP32 on Serial Monitor at boot.
ESP32_IP   = os.getenv("ESP32_IP", "spidercam.local")
ESP32_PORT = int(os.getenv("ESP32_PORT", 80))
ESP32_BASE_URL = f"http://{ESP32_IP}:{ESP32_PORT}"

# Timeout in seconds for each HTTP call to the ESP32.
ESP32_TIMEOUT = 5

# ── Camera ────────────────────────────────────────────────────────────────────
# Switch between "mlx" (MLX90640) and "flir" (FLIR One Pro) here.
CAMERA_TYPE = os.getenv("CAMERA_TYPE", "flir")

# MLX90640 — connected via I²C
MLX_I2C_ADDR = 0x33       # Default I²C address
MLX_FRAME_RATE = 4        # Hz (valid values: 1, 2, 4, 8, 16, 32, 64)

# FLIR One Pro — read from the v4l2loopback devices the flirone-v4l2 C driver
# creates (NOT the raw USB device — the driver demuxes USB → loopback).
#   /dev/video1  Y8 thermal,    160×120 (per-frame contrast-stretched)
#   /dev/video2  MJPEG visible, 1440×1080
#   /dev/video3  RGB24 colorized thermal (driver-side palette)
# See flir-one-viewer/README.md for the full pipeline.
FLIR_DEVICE        = os.getenv("FLIR_DEVICE", "/dev/video1")          # thermal
FLIR_VISUAL_DEVICE = os.getenv("FLIR_VISUAL_DEVICE", "/dev/video2")   # visible

# Resolution served to the browser for both feeds (downscaled from source).
FLIR_DISPLAY_W = 640
FLIR_DISPLAY_H = 480
FLIR_JPEG_QUALITY = 80

# Temperature window the thermal palette maps onto, in °C. The /dev/video1
# feed is a per-frame contrast-stretched Y8 image; absolute per-pixel temps
# are NOT recoverable from it (the C driver burns only min/med/max text into
# the frame and keeps the raw16 + Planck calibration internal). So the temp
# stats the UI shows are an APPROXIMATE linear mapping of Y8 intensity onto
# this window. These endpoints also drive the iron palette legend, so keep
# them matched to it. To get true calibrated temps, expose raw16 from the
# C driver (see README · "Argus Backend Integration").
FLIR_TEMP_MIN_C = float(os.getenv("FLIR_TEMP_MIN_C", 12.0))
FLIR_TEMP_MAX_C = float(os.getenv("FLIR_TEMP_MAX_C", 78.0))
# Frame is flagged as containing an anomaly when its max temp exceeds this.
ANOMALY_TEMP_C  = float(os.getenv("ANOMALY_TEMP_C", 55.0))

# The patched flirone-v4l2 driver publishes the real per-frame temperatures
# (min/mean/max/spot in °C, computed from raw16 via plank.h) to this file each
# frame, since they can't be recovered from the contrast-stretched Y8 feed.
# When present and fresh, Argus reports these instead of the Y8 approximation.
# (Absolute values are still ~a few °C off — the camera's factory calibration
# isn't exposed; see flir-one-viewer README "Temperatures are off by several °C".)
FLIR_TEMPS_FILE    = os.getenv("FLIR_TEMPS_FILE", "/tmp/flir_temps")
FLIR_TEMPS_MAX_AGE = 3.0   # seconds; older than this falls back to the Y8 estimate

# ── Inspection ────────────────────────────────────────────────────────────────
# How often (ms) the Pi polls the camera + ESP32 position during a pass.
CAPTURE_INTERVAL_MS = 250

# ── Data paths ────────────────────────────────────────────────────────────────
BASE_DATA_DIR   = os.path.join(os.path.dirname(__file__), "..", "data")
INSPECTIONS_DIR = os.path.join(BASE_DATA_DIR, "inspections")
BASELINES_DIR   = os.path.join(BASE_DATA_DIR, "baselines")
BASELINE_LATEST = os.path.join(BASELINES_DIR, "latest")
# Sample frames for trying the comparison pipeline without the gantry.
DEMO_DIR          = os.path.join(BASE_DATA_DIR, "demo")
DEMO_BASELINE_DIR = os.path.join(DEMO_DIR, "baseline")
DEMO_CURRENT_DIR  = os.path.join(DEMO_DIR, "current")

# Directories the app ensures exist at startup (see app.main).
DATA_DIRS = (INSPECTIONS_DIR, BASELINES_DIR, DEMO_BASELINE_DIR, DEMO_CURRENT_DIR)

# ── Leak detection ────────────────────────────────────────────────────────────
# Temperature delta (°C) above which a pixel is considered anomalous.
LEAK_THRESHOLD = 2.0

# Spatial smoothing kernel size before comparison (must be odd).
NOISE_FILTER_KERNEL = 3

# Minimum number of anomalous pixels to trigger a leak alert
# (filters single-pixel sensor noise).
MIN_ALERT_AREA = 4

# ── Web server ────────────────────────────────────────────────────────────────
FLASK_HOST = "0.0.0.0"   # Listen on all interfaces so laptop can reach Pi
FLASK_PORT = 5000
FLASK_DEBUG = False

# ── Argus mission / gantry ─────────────────────────────────────────────────────
MISSION_NAME = os.getenv("MISSION_NAME", "OMV-A · Gas Distribution Hub 4")
SESSION_ID   = int(os.getenv("SESSION_ID", 47))

# Plant footprint the coverage map represents (mm). locNorm pins map into this.
PLANT_W_MM = 800
PLANT_H_MM = 800

# Autonomous serpentine scan shape (cells = passes × cells-per-pass).
SCAN_TOTAL_PASSES = 12
SCAN_CELLS_PER_PASS = 8
SCAN_TOTAL_CELLS = SCAN_TOTAL_PASSES * SCAN_CELLS_PER_PASS
# Seconds of (simulated) wall-clock for a full scan, used to advance the demo
# progress/elapsed/eta when no real path planner is running.
SCAN_DURATION_S = 620

# Manual control.
JOG_STEP_MM   = 10        # one tap = this many mm
DEFAULT_SPEED = 45        # 0..100 motor speed on startup
Z_MIN_MM = 0
Z_MAX_MM = 2000

# ── SocketIO push rates (Hz / seconds) — see Argus Spec §8 ──────────────────────
PUSH_STATUS_PERIOD_S   = 2.0      # status_update   (every 2 s)
PUSH_POSITION_HZ       = 10.0     # position_update (10 Hz)
PUSH_FRAME_STATS_HZ    = 8.0      # frame_stats     (~8 Hz, per frame)
PUSH_SCAN_PROGRESS_HZ  = 1.0      # scan_progress   (1 Hz)
