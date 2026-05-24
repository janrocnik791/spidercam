"""
SpiderCam Pi – FLIR One Pro Camera service

Reads the two v4l2loopback devices the ``flirone-v4l2`` C driver publishes
(it owns the USB link and demuxes the camera's proprietary dual stream):

    /dev/video1   Y8 thermal   160×120   (per-frame contrast-stretched)
    /dev/video2   MJPEG visible 1440×1080

Each device is drained by its own background thread that always holds the most
recent frame, so a slow HTTP client can never stall capture and the Pro's
periodic USB drops just freeze the last frame instead of crashing the stream
(same decoupling the proven flir-one-viewer/viewer.py uses).

Thermal colouring: the driver gives us Y8 (R=G=B). We collapse to grey,
contrast-stretch, then map through an IRON look-up table built from the exact
control colours of the Argus temp-tape legend, so the live feed and the legend
agree pixel-for-pixel.

Temperature stats: see the long note on ``get_thermal_stats`` — the loopback
Y8 feed does not carry absolute temperatures, so the min/avg/max we report are
an approximate, auto-ranged mapping of intensity onto the configured °C window.
"""

import logging
import os
import threading
import time

import cv2
import numpy as np

from app import config
from .camera_base import CameraBase

log = logging.getLogger(__name__)


def _build_iron_lut():
    """256-entry BGR LUT matching the Argus legend gradient.

    Legend stops (CSS, warm→ order): #1a0a2e #5a1a3a #c43a1f #ff8a3d #ffd23a
    #fff8d0. We interpolate them across 0..255 and store as BGR for OpenCV.
    """
    stops_rgb = [
        (0x1a, 0x0a, 0x2e),
        (0x5a, 0x1a, 0x3a),
        (0xc4, 0x3a, 0x1f),
        (0xff, 0x8a, 0x3d),
        (0xff, 0xd2, 0x3a),
        (0xff, 0xf8, 0xd0),
    ]
    xs = np.linspace(0, 255, len(stops_rgb))
    grid = np.arange(256)
    rgb = np.stack([np.interp(grid, xs, [s[c] for s in stops_rgb]) for c in range(3)], axis=1)
    bgr = rgb[:, ::-1]
    return bgr.astype(np.uint8)


_IRON_LUT = _build_iron_lut()


class _Reader:
    """Background V4L2 reader that always exposes the latest frame + an fps."""

    def __init__(self, device, label):
        self.device = device
        self.label = label
        self.index = _device_index(device)
        self._frame = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._fps = 0.0
        self._fps_window = []
        self._thread = threading.Thread(target=self._loop, name=f"flir-{label}", daemon=True)

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()

    def _open(self):
        """Open the loopback device, retrying forever — the C driver may not be
        up yet, or the Pro may be mid-reconnect. Never crashes the process."""
        attempt = 0
        while not self._stop.is_set():
            attempt += 1
            cap = cv2.VideoCapture(self.index, cv2.CAP_V4L2)
            if cap.isOpened():
                ok, _ = cap.read()
                if ok:
                    log.info("FLIR %s: %s ready", self.label, self.device)
                    return cap
                cap.release()
            if attempt <= 3 or attempt % 20 == 0:
                log.warning("FLIR %s: waiting for %s (attempt %d)…",
                            self.label, self.device, attempt)
            time.sleep(0.5)
        return None

    def _loop(self):
        cap = self._open()
        if cap is None:
            return
        try:
            while not self._stop.is_set():
                ok, frame = cap.read()
                if not ok or frame is None:
                    time.sleep(0.05)
                    continue
                now = time.monotonic()
                self._fps_window.append(now)
                self._fps_window = [t for t in self._fps_window if now - t <= 1.0]
                with self._lock:
                    self._frame = frame
                    self._fps = float(len(self._fps_window))
        finally:
            cap.release()

    def latest(self):
        with self._lock:
            return None if self._frame is None else self._frame.copy()

    def fps(self):
        with self._lock:
            return round(self._fps, 1)


def _device_index(device):
    """'/dev/video1' -> 1 ; pass through ints/numeric strings unchanged."""
    s = str(device)
    digits = "".join(ch for ch in s if ch.isdigit())
    return int(digits) if digits else 0


class FLIROneProCamera(CameraBase):
    def __init__(self):
        self._thermal = _Reader(config.FLIR_DEVICE, "thermal").start()
        self._visual = _Reader(config.FLIR_VISUAL_DEVICE, "visual").start()
        self._lo = config.FLIR_TEMP_MIN_C
        self._hi = config.FLIR_TEMP_MAX_C

    # ── thermal ────────────────────────────────────────────────────────────────
    def _grey(self, frame_bgr):
        """v4l2loopback surfaces Y8 as 3-channel BGR (R=G=B); collapse to grey."""
        if frame_bgr.ndim == 3:
            return cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2GRAY)
        return frame_bgr

    def get_thermal_frame(self):
        """Return ``(bgr_frame, stats)`` for the iron-colorized thermal feed.

        ``bgr_frame`` is display-ready BGR at the configured display size;
        ``stats`` is ``{min, avg, max}`` in °C. Returns ``(None, None)`` when no
        thermal frame is available yet (driver still starting / camera dropped).
        """
        raw = self._thermal.latest()
        if raw is None:
            return None, None
        grey = self._grey(raw)
        stats = self._stats_from_grey(grey)
        # Contrast-stretch for display, then iron LUT.
        lo, hi = int(grey.min()), int(grey.max())
        if hi > lo:
            stretched = ((grey.astype(np.int32) - lo) * 255 // (hi - lo)).astype(np.uint8)
        else:
            stretched = grey
        color = _IRON_LUT[stretched]
        color = cv2.resize(color, (config.FLIR_DISPLAY_W, config.FLIR_DISPLAY_H),
                           interpolation=cv2.INTER_NEAREST)
        return color, stats

    def _read_real_temps(self):
        """Read the driver's real per-frame temps (°C), or None if unavailable.

        The patched flirone-v4l2 driver writes "min mean max spot" to
        config.FLIR_TEMPS_FILE each frame. We only trust it when it's fresh, so
        a dead driver doesn't leave us reporting a stale temperature forever.
        """
        path = config.FLIR_TEMPS_FILE
        try:
            if time.time() - os.path.getmtime(path) > config.FLIR_TEMPS_MAX_AGE:
                return None
            with open(path) as f:
                parts = f.read().split()
            mn, avg, mx, spot = (float(p) for p in parts[:4])
            return {"min": round(mn, 1), "avg": round(avg, 1),
                    "max": round(mx, 1), "spot": round(spot, 1)}
        except (OSError, ValueError):
            return None

    def _stats_from_grey(self, grey):
        """min/avg/max in °C + a live anomaly flag.

        Temperatures: prefer the driver's real values (raw16 → °C via plank.h);
        only if that file is missing/stale do we fall back to an approximate
        linear mapping of the contrast-stretched Y8 intensity onto the
        configured [FLIR_TEMP_MIN_C, FLIR_TEMP_MAX_C] window.

        Anomaly: a *localized hotspot* heuristic on the (stretched) Y8 — a
        small-but-nonzero cluster of pixels well above the frame median. This
        is robust to absolute calibration and toggles as the camera pans. The
        authoritative anomaly state still belongs to the detection pipeline.
        """
        g = grey.astype(np.float32)
        med = float(np.median(g))
        mx_grey = float(g.max())
        hot_cut = med + 0.6 * (mx_grey - med)
        hot_frac = float((g > hot_cut).mean()) if mx_grey > med else 0.0
        anomaly = 0.0005 < hot_frac < 0.15

        real = self._read_real_temps()
        if real is not None:
            return {"min": real["min"], "avg": real["avg"], "max": real["max"],
                    "anomaly": bool(anomaly)}

        # Fallback: Y8 → °C approximation (no calibrated temps available).
        span = self._hi - self._lo
        to_c = lambda v: round(self._lo + (float(v) / 255.0) * span, 1)
        return {"min": to_c(g.min()), "avg": to_c(g.mean()), "max": to_c(mx_grey),
                "anomaly": bool(anomaly)}

    def get_thermal_stats(self):
        raw = self._thermal.latest()
        if raw is None:
            return None
        return self._stats_from_grey(self._grey(raw))

    def get_thermal_y8(self):
        """Return the raw Y8 thermal frame as a single-channel uint8 array, or
        ``None`` if no frame is available yet.

        This is the un-colorized intensity image the inspection pipeline saves
        and diffs. (``get_thermal_frame`` returns an iron-LUT BGR image for
        display; grayscaling that would not recover the Y8 ordering.)
        """
        raw = self._thermal.latest()
        if raw is None:
            return None
        return self._grey(raw)

    # ── visual ─────────────────────────────────────────────────────────────────
    def get_visual_frame(self):
        """Return the visible-light frame as display-sized BGR, or None.

        The driver declares /dev/video2 at the Pro's true 1440×1080, so OpenCV
        decodes the full JPEG and the frame is NOT black (verified on-device).
        We downscale to the display size for streaming.
        """
        frame = self._visual.latest()
        if frame is None:
            return None
        if frame.shape[:2] != (config.FLIR_DISPLAY_H, config.FLIR_DISPLAY_W):
            frame = cv2.resize(frame, (config.FLIR_DISPLAY_W, config.FLIR_DISPLAY_H))
        return frame

    # ── fps for the REC indicator / Pi status ────────────────────────────────────
    def thermal_fps(self):
        return self._thermal.fps()

    def visual_fps(self):
        return self._visual.fps()

    def is_streaming(self):
        return self._thermal.latest() is not None or self._visual.latest() is not None

    # ── CameraBase interface (kept for compatibility with get_camera()) ──────────
    def get_frame(self):
        """Approximate thermal field in °C as a float32 (H, W) array."""
        raw = self._thermal.latest()
        if raw is None:
            return np.zeros((120, 160), dtype=np.float32)
        g = self._grey(raw).astype(np.float32)
        return self._lo + (g / 255.0) * (self._hi - self._lo)

    def get_resolution(self):
        return (160, 120)

    def close(self):
        self._thermal.stop()
        self._visual.stop()
