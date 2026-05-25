"""
SpiderCam Pi – Central Configuration
All tuneable values live here. Never hardcode these elsewhere.
"""

import os

# ── ESP32 ─────────────────────────────────────────────────────────────────────
# Reach the ESP32 motor controller by its mDNS hostname rather than a raw IP, so
# the link keeps working when the phone-hotspot DHCP hands it a new address — it
# re-resolves "spidercam.local" by name (the ESP32 advertises this via ESPmDNS;
# it also pins a static IP as a fallback). Override ESP32_IP with a bare host or
# IP (NO scheme — the http:// is added below) if mDNS isn't available, e.g.
# ESP32_IP=192.168.85.85.
ESP32_IP   = os.getenv("ESP32_IP", "spidercam.local")
ESP32_PORT = int(os.getenv("ESP32_PORT", 80))
ESP32_BASE_URL = f"http://{ESP32_IP}:{ESP32_PORT}"   # -> http://spidercam.local:80

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

# Empirical calibration of the driver's temps. The flirone-v4l2 driver computes
# °C from raw16 using GENERIC placeholder Planck constants (plank.h), not this
# camera's factory calibration, so readings run several °C high (e.g. skin reads
# ~42°C instead of ~37°C). We correct each driver temperature linearly:
#     T_corrected = FLIR_TEMP_SCALE * T_raw + FLIR_TEMP_OFFSET_C
# Default offset −5°C anchors a face reading (42 → 37). To refine: with two
# known references (T_true, T_raw) solve scale & offset; with one, set scale=1
# and offset = T_true − T_raw.
FLIR_TEMP_SCALE    = float(os.getenv("FLIR_TEMP_SCALE", 1.0))
FLIR_TEMP_OFFSET_C = float(os.getenv("FLIR_TEMP_OFFSET_C", -5.0))

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

# Minimum flagged-region area (pixels) to trigger a leak alert. A real thermal
# anomaly is a sizeable warm blob; tiny values let isolated sensor noise through
# (4 px gave ~63% false positives on a static scene). ~48 px ≈ a 7×7 region on
# the 120×160 frame.
MIN_ALERT_AREA = 48

# Two flagged regions whose bbox centres are within this many pixels (Manhattan,
# in the 160×120 thermal frame) are treated as the same hotspot and de-duplicated
# within a pass. With a fixed camera a leak sits at the same image location in
# every frame, so this collapses it to one alert per pass instead of one per cell.
IMAGE_MERGE_DISTANCE = int(os.getenv("IMAGE_MERGE_DISTANCE", 24))

# Register the baseline to the current frame (SIFT homography) before diffing.
# OFF by default: the camera is fixed, so frames are already aligned, and SIFT on
# low-texture thermal frames invents warps that misalign the static scene and
# create large false diffs. Enable only for a moving rig that needs registration.
ALIGN_FRAMES = False

# Absolute hot-spot gate: a changed region only counts as a leak if its
# reconstructed temperature exceeds this (°C). A real leak (e.g. the demo's
# heated resistor on aluminium) is hot, so this rejects cool-scene changes —
# noise, lighting, someone walking through — that aren't an actual hotspot.
# Per-pixel °C is reconstructed from the Y8 stretch + the frame's calibrated
# min/max temps (written as a .temp sidecar by the runner). Set to None to
# disable the gate; the gate is also skipped when no temp data is available.
LEAK_TEMP_MIN_C = float(os.getenv("LEAK_TEMP_MIN_C", 30.0))

# Absolute hot-spot mode. When ON, the comparator skips the baseline diff and
# flags any region in the CURRENT frame hotter than LEAK_TEMP_MIN_C — so a
# steadily-hot leak (e.g. the demo resistor) is reported on EVERY pass, not only
# the pass where it changes. When OFF (default) it flags changes-vs-baseline
# gated by temperature. The area filter + image-space dedup apply in both.
# Enable for the demo with:  LEAK_ABSOLUTE_MODE=1 LEAK_TEMP_MIN_C=<measured> ...
LEAK_ABSOLUTE_MODE = os.getenv("LEAK_ABSOLUTE_MODE", "0").lower() in ("1", "true", "yes", "on")

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
# progress/elapsed/eta when no real path planner is running. Everything derived
# from progress (percentage, SWEPT %, coverage map, current cell/pass, elapsed,
# ETA, simulated head position) scales with this — so a short value runs the
# whole sim quickly for testing. Set to ~1 min so a full pass is observable.
SCAN_DURATION_S = 60

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
