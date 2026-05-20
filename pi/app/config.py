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
CAMERA_TYPE = os.getenv("CAMERA_TYPE", "mlx")

# MLX90640 — connected via I²C
MLX_I2C_ADDR = 0x33       # Default I²C address
MLX_FRAME_RATE = 4        # Hz (valid values: 1, 2, 4, 8, 16, 32, 64)

# FLIR One Pro — connected via USB; set to the correct /dev/video* device
FLIR_DEVICE = os.getenv("FLIR_DEVICE", "/dev/video0")

# ── Inspection ────────────────────────────────────────────────────────────────
# How often (ms) the Pi polls the camera + ESP32 position during a pass.
CAPTURE_INTERVAL_MS = 250

# ── Data paths ────────────────────────────────────────────────────────────────
BASE_DATA_DIR   = os.path.join(os.path.dirname(__file__), "..", "data")
INSPECTIONS_DIR = os.path.join(BASE_DATA_DIR, "inspections")
BASELINES_DIR   = os.path.join(BASE_DATA_DIR, "baselines")
BASELINE_LATEST = os.path.join(BASELINES_DIR, "latest")

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
