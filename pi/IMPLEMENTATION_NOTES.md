# Argus Backend — Implementation Notes (session handoff)

Running log of everything changed in this work session, so a future session knows
**what** was done and **how**. All paths are relative to `pi/`. Nothing here is
committed yet (HEAD is `89b29b4 "Upgraded comparator"`); these are working-tree
edits.

---

## 0. Current runtime state / how it's being run

- The server is normally launched **directly**, not via `start_argus.sh`, to dodge
  a driver-restart race (see Gotchas):
  ```
  LEAK_ABSOLUTE_MODE=1 PYTHONPATH=/home/pi/projects/spidercam/pi /usr/bin/python3 -m app.main > /tmp/argus-direct.log 2>&1 &
  ```
  (system python `/usr/bin/python3` has cv2/numpy/flask; the `.venv` is for the
  unused MLX path only.) LAN URL: `http://192.168.85.249:5000`.
- The FLIR driver (`flir-one-viewer/driver/flirone-v4l2 <palette>`) is a **separate
  process**; it writes the grayscale thermal to `/dev/video1` and per-frame temps
  to `/tmp/flir_temps`.
- **As of end of session the FLIR One is UNPLUGGED** (`lsusb` has no `09cb:1996`;
  driver logs "Could not find/open device"). No live feed until it's reconnected
  and the driver restarted. The server is up in absolute mode awaiting it.

---

## 1. Config knobs (app/config.py) — current values

| Constant | Value | Meaning |
|---|---|---|
| `SCAN_DURATION_S` | **60** | full simulated scan time (was 620). Drives progress %, coverage map, cells/passes, elapsed/ETA, sim head position. |
| `NOISE_FILTER_KERNEL` | 3 | median-blur kernel applied before diffing (now actually used). |
| `MIN_ALERT_AREA` | **48** px | min flagged-region area (was 4 → ~63% false hits). |
| `IMAGE_MERGE_DISTANCE` | 24 px | dedup: same-image-location hotspots merge to 1 alert/pass. |
| `ALIGN_FRAMES` | **False** | SIFT homography registration. OFF = direct compare (fixed camera). |
| `LEAK_TEMP_MIN_C` | 30.0 | absolute hot gate: a region must reconstruct hotter than this. |
| `LEAK_ABSOLUTE_MODE` | env `0` | when on, flag hot regions directly (no baseline). |
| `FLIR_TEMP_SCALE` / `FLIR_TEMP_OFFSET_C` | 1.0 / **-5.0** | linear calibration of driver temps. |
| `FLIR_TEMP_MIN_C` / `FLIR_TEMP_MAX_C` | 12 / 78 | Y8→°C window (fallback only; driver temps preferred). |

All overridable via env. `MIN_ALERT_AREA` and `IMAGE_MERGE_DISTANCE` are *bound at
import* in `leak_detector.py` → **restart needed** to change them.

---

## 2. Changes by subsystem

### A. Scan state machine — `app/services/runtime.py`
- **Boot idle**: `ScanState.__init__` now sets `mode = "idle"` (was `"autonomous"`,
  which made the sim auto-advance on every boot). Scan only leaves idle on an
  explicit start.
- **`cell_center(cell_index)`** added: deterministic `(x,y)` mm for a 1-based
  serpentine cell (mirrors `_sim_position` direction). Used to name captured
  frames so the same cell → same filename across passes. Purely time-derived (no
  ESP). cell 1 → (50,33), cell 96 → (50,767).

### B. Inspection runner — `app/services/inspection_runner.py` (rewritten, time-based)
- **No ESP** in the capture loop (removed `_current_position`/`_esp_idle` and their
  ~5s blocking calls that throttled capture to 1 frame/10s).
- **One frame per simulated cell**: polls `runtime.scan.snapshot()["current_cell"]`;
  on a new cell, grabs the Y8 frame, names it `{x}_{y}.jpg` via `cell_center`,
  saves it, compares live. ~1.6 fps over the 60 s scan (≤96 frames).
- **Auto-finalize at 100%**: loop breaks when `runtime.scan.mode == "complete"`
  (or on `stop()`); idles while `paused`/`estop`. `stop()` is now instant (no ESP).
- **`.temp` sidecar**: `_write_temp_sidecar` writes `"<min> <max>"` (calibrated)
  next to each frame, so the comparator can reconstruct per-pixel °C on both the
  live and batch paths.
- **Baseline is a real copy, not a symlink**: `_update_baseline` copies the raw
  `{x}_{y}.jpg` frames into `data/baselines/latest/` (a fresh dir), **excluding**
  `_baseline`/`_diff`/`_current` overlays. Previously it symlinked the whole pass
  dir → "same files in both places" confusion + overlay clutter.
- Pause/resume already respected via the `mode != "autonomous"` idle guard.
- Removed the temporary `[Runner]` debug prints that were added to diagnose the
  "no files" issue (root cause was a dead FLIR driver).

### C. Comparator — `app/detection/comparator.py`
- **SIFT alignment OFF by default** (`config.ALIGN_FRAMES`): it was the root cause of
  ~19/30 false positives — on low-texture thermal frames SIFT fit garbage
  homographies that misaligned the static baseline. Direct compare → 0 false
  positives. `_align_gray` kept for a future moving rig.
- **Median denoise** in `_diff_mask` (kills flickering sensor pixels).
- **Absolute hot gate**: `_hot_mask(current, temp_range, min_temp_c)` reconstructs
  per-pixel °C from the Y8 linear stretch + the frame's calibrated `[T_min,T_max]`
  (from the `.temp` sidecar via `_read_temp_sidecar`). `_apply_hot_gate` ANDs that
  with the diff → a region must be *changed AND hot* to flag.
- **Absolute hot mode** (`config.LEAK_ABSOLUTE_MODE`): `_detect_absolute` flags hot
  regions in the current frame directly (no baseline) → a steadily-hot leak (the
  demo resistor) is reported every pass. The driver's burned-in crosshair/markers
  (Y8≈255) are too small (5×7 glyphs) to pass `MIN_ALERT_AREA`, so no masking is
  needed.
- Overlays (`_diff.jpg`/`_baseline.jpg`) are saved **only when there's an alert**.
- `_parse_coord` accepts both `{x}_{y}.jpg` and legacy `frame_{n}.jpg`.
- **Note on detection model**: it is *change-vs-baseline* by default; a constantly
  hot object that's also hot in the baseline produces no diff → use absolute mode
  for steady-state detection.

### D. Leak detector — `app/detection/leak_detector.py`
- **Image-space dedup**: dedups per pass by bbox centroid in the frame
  (`IMAGE_MERGE_DISTANCE` px) **in addition to** the legacy gantry-coordinate dedup
  (`_ZONE_MERGE_DISTANCE`, a no-op for the fixed camera). Collapses a fixed-camera
  hotspot (same pixels every cell) to one alert/pass. `reset_alert_zones()` clears
  both zone lists.

### E. Detection record — `app/routes/inspection.py`
- `_alert_to_detection` now includes `tempVal` (approx °C via the Y8→°C window of
  `max_delta`), `delta`, `confidence` (`min(1, area/(MIN_ALERT_AREA*10))`), and
  `area` (bbox px) — the history-detail view reads these.
- `_frame_files` maps `current` → `{x}_{y}.jpg` (the runner's frame), not a copy.

### F. SocketIO control — `app/events.py`
- Added **`start_scan`** handler (mirrors `POST /api/inspection/start`):
  `runtime.scan.start()` + `_start_runner()` + push `scan_progress`. The UI's START
  button (`emit('start_scan')`) now actually starts a pass; UI tracks live state
  via the `scan_progress` stream.
- `stop_scan` and `estop` now also call `_stop_runner()` (so a pass finalizes:
  batch compare + baseline refresh). `_start_runner`/`_stop_runner` are lazy-imported
  from `routes/inspection.py` to avoid an import cycle.

### G. Temperature calibration — `app/services/camera_flir.py` + config
- Driver temps run several °C high because `plank.h` uses **generic placeholder
  Planck constants**, not this camera's factory calibration. Fix: linear
  correction `T' = FLIR_TEMP_SCALE*T + FLIR_TEMP_OFFSET_C` applied in
  `_read_real_temps` (default `-5°C` → a 42°C face reads ~37). Tune with 1 ref
  (offset) or 2 refs (scale+offset). `/dev/video1` Y8 is confirmed a per-frame
  *linear* stretch of `[T_min,T_max]`, which validates the per-pixel reconstruction.

### H. Frontend — `app/static/Argus.html` + `v6-argus.jsx`
- Canvas **1440×900 → 1920×1080** (`#design` + `fit()` math in Argus.html; root div
  in v6-argus.jsx; right rail 360→440px; history strip 180→220px).
- **Cache-busting** in `app/__init__.py`: the `/` route now serves Argus.html with
  `?v=<file-mtime>` appended to each local `.jsx` script src and `Cache-Control:
  no-store`. So browsers always fetch fresh JSX after an edit — **no server restart
  needed** for static-file changes (Flask reads disk per request).
- **IDLE state** in `V6InspectionDetails`: READY pill, "READY TO SCAN" /
  "AWAITING START", zero progress and `——:——` elapsed/ETA when idle.
- **START INSPECTION button** when idle/complete (`onStart → emit('start_scan')`),
  else PAUSE/RESUME + STOP.
- **`complete` state**: "SCAN COMPLETE", and the START button returns so you can
  run again.
- **Real frame thumbnails**: `V6FrameThumb` (an `<img>` with `onError`/missing →
  fallback to placeholder `HistoryThumb`, forwards `w`/`h`). History card: PAST →
  `frames.past`, CURRENT → `frames.difference`. Alarm modal HOTSPOT → `frames.difference`.
  Backend serves these at `/api/results/detections/<id>/frame/<past|current|difference>`.

### I. `start_argus.sh`
- Added a **restart guard**: kills any existing `app.main` bound to `:5000` before
  launching, so re-running the script actually replaces the server (it previously
  failed to bind and left the stale one serving).

---

## 3. Operational gotchas (hard-won — read before debugging)

- **`start_argus.sh` driver EXIT-trap race**: when the script starts the FLIR
  driver it traps EXIT to kill it. On a restart, the *old* script exiting can kill
  the driver the *new* run is reusing → no camera. Workaround used all session:
  start the driver **directly** (no trap) and run the server **directly**.
- **`pkill -f` self-match**: a `pkill -f 'app.main'` / `'flirone-v4l2'` matches the
  killing shell's own command line and SIGKILLs it (exit 143/144 with no output).
  Use a bracket trick (`'[a]pp\.main'`, `'[f]lirone-v4l2'`) and never put the
  launch text in the same command as the kill.
- **Camera reconnect after a server restart** sometimes gives `select() timeout`
  (stale loopback handle) — fix is a fresh driver + fresh server. If `/tmp/flir_temps`
  is fresh (age ~0) the driver is alive and processing USB; if stale, it isn't.
- **Headless chromium hangs on this Pi** (`--dump-dom` times out; the Debian wrapper
  injects a flag the renderer rejects). Can't visually verify renders here — verify
  served bytes + logic instead.
- **ESP32 is offline** (`spidercam.local` doesn't resolve; 5s timeout per call). The
  runner is now ESP-free so this no longer matters for capture; manual-jog socket
  commands still hit it.
- **leak_detector binds config values at import** → restart to change MIN_ALERT_AREA
  / IMAGE_MERGE_DISTANCE.

---

## 4. Verifying without the UI

- Socket control test pattern: a `socketio.Client()` (python-socketio is installed;
  `websocket-client` is NOT, so it uses polling — fine) connecting to
  `http://localhost:5000`, emit `start_scan`, poll `GET /api/inspection/status` for
  `mode`, watch `scan_progress`/`new_detection`, emit `stop_scan`.
- Detection logic: drive `comparator.compare_frames(baseline, current, x, y)` on
  synthetic frames + a `.temp` sidecar; reset `leak_detector.reset_alert_zones()`
  between cases. Validated: static scene → 0; hot blob → 1; cool change → 0; same
  hotspot ×N cells → 1; real-size driver glyphs → 0.

---

## 5. Open / suggested next steps
- **Demo**: enable `LEAK_ABSOLUTE_MODE=1`, point camera at the heated resistor, read
  the UI MAX temp, set `LEAK_TEMP_MIN_C` a few °C below it (above ambient).
- Reconnect the FLIR One and restart the driver to restore the live feed.
- Optional: fix the `start_argus.sh` driver trap so normal launches don't kill the
  driver on restart; refine temp calibration with a 2-point reference; add temporal
  persistence (N consecutive frames) on top of image-space dedup if needed.
