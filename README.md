# SpiderCam — Thermal Inspection Gantry

A cable-driven **SpiderCam** gantry that carries a thermal camera over an area
and streams a live operator view to any browser on the LAN. Four cables, one
per corner, are reeled by an ESP32 to move the camera head in X/Y/Z. A
Raspberry Pi hosts the camera and the web stack.

The thermal camera is a **FLIR One Pro** (USB-C). On the Pi, a libusb C driver
demuxes its proprietary dual stream into standard `v4l2loopback` devices, and a
Flask/SocketIO app reads those devices and serves the **Argus** operator UI.

> **Status (May 2026):** thermal feed is working. Visual (optical) feed is in
> progress. The Argus backend (motor control, inspection runs, leak detection,
> live data wiring) is **not yet implemented** — see [Current status](#current-status).

---

## System overview

```
[ browser on LAN ]
       │  HTTP / MJPEG (and SocketIO, once wired)
       ▼
[ Raspberry Pi 5 ] ──────────── Wi-Fi ───────────▶ [ ESP32 ]
  pi/flir-one-viewer/                                4× DRV8825 steppers
    flirone-v4l2 (C, libusb)                         reels the 4 cables
      └▶ /dev/video1  Y8 thermal   160×120           HTTP API on :80
         /dev/video2  MJPEG visual 1440×1080         (mDNS: spidercam.local)
         /dev/video3  RGB colorized thermal
  pi/app/  (Flask + SocketIO)
    serves the Argus operator UI
       ▲
       │  USB-C
[ FLIR One Pro ]  (VID:PID 09cb:1996)
```

The Pi is the **brain** (camera, web server, future detection pipeline). The
ESP32 is the **muscle** (owns the motors, runs cable-length inverse kinematics,
reports position). The FLIR One Pro plugs into the Pi over USB-C.

---

## Folder structure

```
spidercam/
├── README.md
├── setup.sh                     # one-time Pi bootstrap (Node, Claude Code, venv, I²C tools)
│
├── esp32/                       # PlatformIO project — motor-controller firmware
│   ├── platformio.ini           # env:esp32dev, deps: ArduinoJson, AsyncTCP
│   └── src/
│       ├── spidercam_esp32_4_stepper.ino   # ACTUAL firmware (see ESP32 section)
│       ├── config.h             # Wi-Fi SSID/password, HTTP port, pins
│       ├── http_server.cpp      # legacy scaffolding stub — superseded by the .ino
│       ├── motor_control.cpp    # legacy scaffolding stub
│       └── position.cpp         # legacy scaffolding stub
│
├── pi/
│   ├── requirements.txt         # Flask app deps
│   ├── .venv/                   # Python virtualenv (created by setup.sh)
│   │
│   ├── flir-one-viewer/         # FLIR One Pro → v4l2loopback driver + MJPEG viewer
│   │   ├── README.md            # full driver/viewer docs + extensive troubleshooting
│   │   ├── driver/
│   │   │   ├── flirone-v4l2.c   # patched libusb C bridge (publishes Y8 thermal + visual)
│   │   │   ├── flirone-v4l2     # built binary
│   │   │   ├── Makefile
│   │   │   └── palettes/        # Iron2.raw / Rainbow.raw / Grayscale.raw colormaps
│   │   ├── setup/
│   │   │   ├── install.sh        # one-time: deps, build, udev rule, v4l2loopback on boot
│   │   │   ├── start.sh          # every run: load loopback, run driver, run viewer
│   │   │   └── 77-flirone-lusb.rules
│   │   └── viewer/
│   │       ├── viewer.py         # Flask MJPEG viewer: thermal + visual on :5000
│   │       └── requirements.txt
│   │
│   └── app/                      # Argus Flask + SocketIO backend
│       ├── __init__.py           # create_app() → (app, socketio); registers blueprints
│       ├── main.py               # entry point: python -m app.main
│       ├── config.py             # ESP32 IP, camera type, thresholds, data paths
│       ├── routes/
│       │   ├── camera.py         # /api/camera/frame|frame.json|stream  (implemented)
│       │   ├── control.py        # /api/move, /api/stop                 (STUB — blueprint only)
│       │   ├── inspection.py     # /api/inspection/*                    (STUB)
│       │   └── results.py        # /api/results/*                       (STUB)
│       ├── services/
│       │   ├── camera_base.py    # CameraBase ABC + get_camera() factory (implemented)
│       │   ├── camera_mlx.py     # MLX90640 driver — legacy, implemented
│       │   ├── camera_flir.py    # FLIR One Pro capture                 (STUB — not implemented)
│       │   ├── esp_client.py     # ESP32 HTTP client — only ping() implemented
│       │   └── inspection_runner.py  # autonomous-pass orchestrator     (STUB)
│       ├── detection/
│       │   ├── comparator.py     # baseline-vs-current diff             (STUB)
│       │   ├── leak_detector.py  # thresholding / alerts                (STUB)
│       │   └── noise_filter.py   # smoothing                            (STUB)
│       └── static/               # Argus front-end (React via Babel, runs on demo data)
│           ├── Argus.html         # entry point — open this to see the UI
│           ├── Argus Spec.txt     # functional spec / contract for backend wiring
│           ├── shared.jsx, v6-argus.jsx, v6-data.jsx, v6-history-detail.jsx
│           ├── Argus Mobile.html, Argus Logo.html
│           └── README.md          # front-end handoff notes
│
└── docs/                         # earlier design notes — partly predate the FLIR pivot
    ├── api.md                    # ESP32 HTTP API contract
    ├── setup-guide.md            # Pi-from-scratch dev setup
    └── wiring.md                 # GPIO / wiring notes (still references MLX90640)
```

> The project began with an MLX90640 I²C sensor and a step-counted X/Y gantry.
> It has since pivoted to the **FLIR One Pro** on a **4-cable SpiderCam** rig.
> The MLX90640 code (`camera_mlx.py`), the `esp32/src/*.cpp` scaffolding, and
> parts of `docs/` are leftovers from that earlier design and are noted as such
> above.

---

## The two parts of the Pi setup

### 1. `flir-one-viewer/` — getting the camera onto the Pi

A libusb C driver (`flirone-v4l2`) speaks the FLIR One Pro's proprietary USB
protocol and writes standard V4L2 devices via `v4l2loopback`:

| device | content |
|---|---|
| `/dev/video1` | Y8 thermal, 160×120 |
| `/dev/video2` | MJPEG visible, 1440×1080 |
| `/dev/video3` | RGB24 colorized thermal |

`viewer/viewer.py` reads those devices with OpenCV, re-encodes them as MJPEG,
and serves a side-by-side / overlay page on `:5000`. The driver in this tree
carries several fixes over upstream (`fnoop/flirone-v4l2`) so the Pro's thermal
device actually publishes, the visual frame size is correct, and the driver
survives the Pro's periodic USB drops. The full story — including a deep
troubleshooting guide — is in **`pi/flir-one-viewer/README.md`**.

One-time install:

```bash
cd pi/flir-one-viewer
bash setup/install.sh        # installs deps, builds the driver, sets up udev + v4l2loopback
# log out / back in (or reboot) so the 'video' group and udev rule apply
```

### 2. `app/` — the Argus backend + operator UI

`pi/app/` is a Flask + SocketIO application intended to be the production
operator interface: it serves the **Argus** front-end (`app/static/`) and will
expose the live camera feed, motor control, inspection runs, and leak-detection
results. The front-end design is complete and runs on demo data; **the backend
that feeds it real data is mostly stubs today** (see [Current status](#current-status)).

---

## ESP32 — motor controller

`esp32/src/spidercam_esp32_4_stepper.ino` is the real, working firmware. It:

- drives **4× DRV8825** stepper drivers (one per cable) over STEP/DIR pins,
- computes cable lengths from a target X/Y/Z via SpiderCam inverse kinematics
  (frame geometry/spool radius are constants at the top of the sketch — edit
  these for your rig),
- moves all four motors synchronously (Bresenham-style step distribution),
- connects over Wi-Fi (STA) and advertises mDNS as **`spidercam.local`**,
- homes X/Y against limit switches on boot.

HTTP API (port 80):

| Endpoint | Method | Body / response |
|---|---|---|
| `/ping` | GET | → `{"status":"ok"}` |
| `/move` | POST | `{"x":<cm>,"y":<cm>,"z":<cm>}` → `202 {"status":"queued"}` (runs on a core-0 task) |
| `/stop` | POST | halts the motor task → `{"ok":true}` |
| `/position` | GET | → `{"x":..,"y":..,"z":..}` (cm) |
| `/status` | GET | → `{"state":"idle"|"moving","x":..,"y":..,"z":..}` |

> Note: this absolute **X/Y/Z coordinate** API is the current contract. The
> `docs/api.md` file and the `esp32/src/*.cpp` stubs describe an older
> direction-based (`left/right/forward/backward`) + `/start_inspection` design
> and are out of date.

Build/flash with PlatformIO (`esp32dev` env). Set your Wi-Fi credentials in
`esp32/src/config.h`.

---

## Running it

### Start the camera feed (works today)

```bash
cd pi/flir-one-viewer
bash setup/start.sh
#   optional palette:  FLIR_PALETTE=$PWD/driver/palettes/Rainbow.raw bash setup/start.sh
#   optional port:     FLIR_PORT=8080 bash setup/start.sh
```

`start.sh` loads `v4l2loopback`, launches the C driver, waits ~3 s for the
camera to enumerate, then runs the MJPEG viewer and prints the LAN URL.

### Open the UI

```
http://192.168.85.249:5000
```

(`start.sh` prints the exact URL for the Pi's current IP.) Thermal shows on the
left, visual on the right, with a side-by-side ↔ overlay toggle.

### Start the Argus backend (in progress)

```bash
cd pi
source .venv/bin/activate
python -m app.main          # serves Flask + SocketIO on :5000
```

> **Heads-up:** the FLIR viewer (`viewer.py`) and the Argus app (`app.main`)
> both default to port **5000**, so they can't run on it at the same time. The
> live feed you can open today is the one served by `flir-one-viewer/`; the
> Argus app is the in-progress replacement and its backend routes are still
> stubs. Run one at a time (or give the viewer a different `FLIR_PORT`) until
> the Argus backend reads the v4l2 devices directly.

---

## Current status

| Area | State |
|---|---|
| FLIR One Pro USB driver → `/dev/video1,2,3` | ✅ working (patched in `flir-one-viewer/`) |
| Thermal MJPEG feed at `:5000` | ✅ working |
| Visual / optical feed | ✅ live — streaming into the Argus UI in the browser via MJPEG |
| ESP32 firmware (cable kinematics + HTTP API) | ✅ implemented |
| Argus front-end (layout, components) | ✅ built — **both video feeds (thermal + visible) confirmed live in the interface**; remaining panels still run on **demo data** (`v6-data.jsx`) |
| Argus served + live-data wiring (SocketIO) | ❌ not yet implemented |
| `app/routes/camera.py` | ✅ implemented — serves the live FLIR thermal + visible feeds as MJPEG (`/api/camera/thermal`, `/api/camera/optical`) that the Argus UI renders in the browser |
| `app/routes/control.py`, `inspection.py`, `results.py` | ❌ blueprints only, no endpoints |
| `app/services/esp_client.py` | ⚠️ only `ping()` — `move/stop/position/status` not implemented |
| `app/services/camera_flir.py` | ✅ implemented — reads the FLIR v4l2 devices (`/dev/video1` thermal, `/dev/video2` visible) and streams **both feeds as live MJPEG into the Argus UI in the browser**, not just on the backend |
| `app/services/inspection_runner.py` | ✅ implemented — captures the Y8 thermal frame at 1 fps to `data/inspections/{ts}/{x}_{y}.jpg`, compares each frame live, and repoints the `baselines/latest` symlink to the new pass on completion |
| `app/detection/*` (comparator, leak_detector, noise_filter) | ✅ implemented — SIFT-aligned `absdiff` comparison (`compare_frames`/`compare_pass`), contour-based `LeakAlert` detection with per-pass Manhattan-distance zone dedup, and a Gaussian noise filter |

**Next steps** are roughly: implement `camera_flir.py` to read `/dev/video1`,
point `routes/camera.py` at it, flesh out `esp_client.py` against the ESP32's
X/Y/Z API, serve `Argus.html` from Flask, and replace the front-end's demo data
with live SocketIO events (the contract is in `app/static/Argus Spec.txt`).
