# SpiderCam Inspection System

A gantry-mounted thermal inspection system. A Raspberry Pi 5 controls movement via an ESP32 over Wi-Fi and captures thermal frames during each inspection pass. Frames are indexed by X-Y coordinate and compared against the previous pass to detect temperature anomalies (potential leaks).

---

## System Overview

```
[ Laptop browser ]
       |
       | HTTP (same LAN)
       v
[ Raspberry Pi 5 ]  ──── Wi-Fi ────>  [ ESP32 ]
  Flask web server                     Motor controller
  MLX90640 / FLIR                      Receives: move commands
  Thermal frames                                  start_inspection
  Leak detection                       Reports:  position (x, y)
                                                  status
```

The Pi is the **brain**: it hosts the interface, drives the camera, stores frames, and runs the comparison algorithm.  
The ESP32 is the **muscle**: it owns the motors, executes movement, and reports position back to the Pi.

---

## Folder Structure

```
spidercam/
├── esp32/
│   └── src/
│       ├── main.ino          # Entry point: WiFi init, HTTP server setup, loop
│       ├── config.h          # SSID, password, server port, pin definitions
│       ├── http_server.cpp   # Route handlers: /ping, /move, /start_inspection, /status
│       ├── motor_control.cpp # Motor driver logic (step, direction, speed)
│       └── position.cpp      # Tracks current X-Y position (step counter)
│
├── pi/
│   ├── app/
│   │   ├── __init__.py       # Flask app factory (create_app())
│   │   ├── main.py           # Entry point: python -m app.main
│   │   ├── config.py         # ESP32 IP, camera type, thresholds, data paths
│   │   │
│   │   ├── routes/
│   │   │   ├── __init__.py
│   │   │   ├── control.py        # POST /api/move, POST /api/stop
│   │   │   ├── inspection.py     # POST /api/inspection/start, GET /api/inspection/status
│   │   │   ├── camera.py         # GET /api/camera/frame (single frame as image)
│   │   │   └── results.py        # GET /api/results/latest (leak alerts + heatmap diff)
│   │   │
│   │   ├── services/
│   │   │   ├── __init__.py
│   │   │   ├── esp_client.py     # HTTP client wrapper for all ESP32 calls
│   │   │   ├── camera_base.py    # Abstract base class: get_frame() -> np.ndarray
│   │   │   ├── camera_mlx.py     # MLX90640 implementation
│   │   │   ├── camera_flir.py    # FLIR One Pro implementation (swap-in later)
│   │   │   └── inspection_runner.py  # Orchestrates: start pass, capture frames, save
│   │   │
│   │   ├── detection/
│   │   │   ├── __init__.py
│   │   │   ├── comparator.py     # Loads baseline + current frames, runs diff
│   │   │   ├── noise_filter.py   # Smoothing / temporal averaging before compare
│   │   │   └── leak_detector.py  # Thresholding, contour finding, alert generation
│   │   │
│   │   └── static/
│   │       ├── index.html        # Single-page UI
│   │       ├── css/
│   │       │   └── style.css
│   │       └── js/
│   │           ├── controls.js   # Direction buttons, autonomous start
│   │           ├── camera.js     # Live thermal frame polling
│   │           └── results.js    # Renders leak alerts on the interface
│   │
│   ├── data/
│   │   ├── inspections/
│   │   │   └── {YYYY-MM-DD_HHMMSS}/   # One folder per inspection pass
│   │   │       └── {x}_{y}.npy        # Raw thermal frame (numpy array) at coord x,y
│   │   └── baselines/
│   │       └── latest -> ../inspections/{most_recent}/   # Symlink to last completed pass
│   │
│   └── requirements.txt
│
└── docs/
    ├── architecture.md   # This file, in more detail
    ├── api.md            # Full ESP32 HTTP API contract
    └── wiring.md         # Physical connections (power, any GPIO used)
```

---

## Communication Protocol: Pi → ESP32

All commands are plain HTTP. The ESP32 runs a lightweight HTTP server (Arduino `WebServer` library).  
The Pi sends requests using Python `requests`. No broker, no MQTT, no WebSockets needed at this stage.

| Endpoint | Method | Body | Description |
|---|---|---|---|
| `/ping` | GET | — | Health check. ESP32 replies `{"status": "ok"}` |
| `/move` | POST | `{"direction": "left"}` | Move one step in given direction. Directions: `left`, `right`, `forward`, `backward` |
| `/stop` | POST | — | Halt all motors immediately |
| `/start_inspection` | POST | — | Triggers ESP32's autonomous inspection route |
| `/position` | GET | — | Returns `{"x": 120, "y": 45}` (step counts) |
| `/status` | GET | — | Returns motor state, busy flag, current position |

The Pi polls `/position` during an inspection pass to know which coordinate to tag each captured frame with.

---

## Data Flow: Inspection Pass

```
1. User presses "Start Inspection" in browser
2. Pi sends POST /start_inspection to ESP32
3. ESP32 begins autonomous route (its own motor logic)
4. Pi starts capture loop:
      every N ms → get_frame() from camera
                 → poll /position from ESP32
                 → save frame as data/inspections/{timestamp}/{x}_{y}.npy
5. When ESP32 returns to home position → inspection complete
6. Pi runs comparison:
      for each frame in current pass:
          load matching frame from baselines/latest/{x}_{y}.npy
          apply noise filter to both
          compute diff
          if max(diff) > LEAK_THRESHOLD → flag as alert
7. Alerts appear on the interface with coordinate, diff heatmap, temp delta
8. Current pass becomes new baseline (or user confirms it manually)
```

---

## Camera Abstraction

Both cameras expose the same interface so swapping requires zero changes elsewhere:

```python
class CameraBase:
    def get_frame(self) -> np.ndarray:
        """Returns a 2D float array of temperatures in Celsius."""
        raise NotImplementedError

    def get_resolution(self) -> tuple[int, int]:
        """Returns (width, height) in pixels."""
        raise NotImplementedError
```

- `camera_mlx.py` → MLX90640 (32×24, ~4 fps via I²C)
- `camera_flir.py` → FLIR One Pro (160×120, up to ~8.7 fps via USB)

Switch by changing one line in `config.py`: `CAMERA_TYPE = "mlx"` or `"flir"`.

---

## Leak Detection Logic

```
raw_current[x,y]   ──┐
                      ├─ noise_filter() ──> smoothed_current
raw_current[x,y-1] ──┘  (temporal avg    
raw_current[x,y+1]       over N frames)  

smoothed_baseline  ──────────────────────> diff = |smoothed_current - smoothed_baseline|

diff > LEAK_THRESHOLD  ──> LeakAlert(x, y, max_delta, diff_heatmap_png)
```

Key configurable values in `config.py`:
- `LEAK_THRESHOLD` — temperature delta in °C that triggers an alert (start with ~2.0)
- `NOISE_FILTER_KERNEL` — spatial smoothing kernel size (start with 3×3 Gaussian)
- `MIN_ALERT_AREA` — minimum number of pixels above threshold to count (filters single-pixel noise)

---

## Phase Roadmap

| Phase | Goal | Files involved |
|---|---|---|
| **1 — Connection** | Pi sends `/ping`, ESP32 responds | `esp32/src/main.ino`, `services/esp_client.py` |
| **2 — Manual control** | Direction buttons work from browser | `routes/control.py`, `static/js/controls.js` |
| **3 — Camera feed** | Live thermal frame visible in browser | `services/camera_mlx.py`, `routes/camera.py` |
| **4 — Inspection pass** | Full pass runs, frames saved to disk | `services/inspection_runner.py` |
| **5 — Leak detection** | Comparison algorithm flags anomalies | `detection/` module |
| **6 — FLIR swap** | Replace camera, keep everything else | `services/camera_flir.py`, `config.py` |

---

## Quick Start (Phase 1)

### ESP32
1. Open `esp32/src/main.ino` in Arduino IDE
2. Fill in `config.h` with your Wi-Fi SSID/password
3. Flash to ESP32
4. Open Serial Monitor — it will print its IP address once connected

### Raspberry Pi
1. `cd pi && pip install -r requirements.txt`
2. Set `ESP32_IP` in `app/config.py` to the IP from the serial monitor
3. `python -m app.main`
4. Open `http://<pi-ip>:5000` from your laptop browser
