#!/usr/bin/env python3
"""FLIR One Pro web viewer.

Reads the Y8 thermal stream from /dev/video1 and the MJPEG visible stream
from /dev/video2 (both fed by the flirone-v4l2 C driver via v4l2loopback),
exposes them as MJPEG endpoints on a Flask server, and serves a single
page at http://<pi-ip>:5000 showing both feeds with a side-by-side ↔
overlay toggle. No plugins needed — modern desktop and mobile browsers
render multipart/x-mixed-replace MJPEG natively in <img> tags.
"""

import argparse
import socket
import sys
import threading
import time

import cv2
import numpy as np
from flask import Flask, Response, render_template_string


VISUAL_W, VISUAL_H = 640, 480
THERMAL_DISPLAY_W, THERMAL_DISPLAY_H = 640, 480
JPEG_QUALITY = 80
STREAM_FPS = 15
OPEN_RETRIES = 20
OPEN_DELAY_S = 0.5


INDEX_HTML = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FLIR One Pro</title>
<style>
  body { margin: 0; background: #111; color: #eee;
         font-family: system-ui, -apple-system, sans-serif; }
  header { padding: 12px 16px; display: flex; align-items: center;
           gap: 16px; flex-wrap: wrap; border-bottom: 1px solid #222; }
  h1 { font-size: 16px; margin: 0; font-weight: 600; letter-spacing: 0.02em; }
  button { background: #2a2a2a; color: #eee; border: 1px solid #444;
           padding: 8px 16px; border-radius: 4px; cursor: pointer;
           font: inherit; }
  button:hover { background: #383838; }
  button:active { background: #1f1f1f; }
  main { padding: 12px; }
  .feeds { display: flex; gap: 12px; flex-wrap: wrap; justify-content: center; }
  .feed { flex: 1 1 320px; max-width: 640px; }
  .feed.hidden { display: none; }
  .feed img { width: 100%; height: auto; display: block; background: #000;
              border-radius: 4px; }
  .label { font-size: 12px; color: #888; padding: 6px 4px; text-align: center;
           letter-spacing: 0.05em; text-transform: uppercase; }
  @media (max-width: 700px) {
    header { padding: 10px 12px; }
    .feed { flex: 1 1 100%; max-width: none; }
  }
</style>
</head>
<body>
<header>
  <h1>FLIR One Pro</h1>
  <button id="mode-toggle" type="button">Switch to overlay</button>
</header>
<main>
  <div class="feeds" id="feeds">
    <div class="feed" id="feed-thermal">
      <div class="label">Thermal (INFERNO)</div>
      <img src="/stream/thermal" alt="thermal feed">
    </div>
    <div class="feed" id="feed-visual">
      <div class="label">Visual</div>
      <img src="/stream/visual" alt="visual feed">
    </div>
    <div class="feed hidden" id="feed-overlay">
      <div class="label">Overlay (thermal &alpha;=0.45)</div>
      <img id="overlay-img" alt="overlay feed">
    </div>
  </div>
</main>
<script>
  (function () {
    const btn = document.getElementById('mode-toggle');
    const thermal = document.getElementById('feed-thermal');
    const visual = document.getElementById('feed-visual');
    const overlay = document.getElementById('feed-overlay');
    const overlayImg = document.getElementById('overlay-img');
    let overlayMode = false;

    btn.addEventListener('click', function () {
      overlayMode = !overlayMode;
      if (overlayMode) {
        btn.textContent = 'Switch to side-by-side';
        thermal.classList.add('hidden');
        visual.classList.add('hidden');
        overlay.classList.remove('hidden');
        overlayImg.src = '/stream/overlay?ts=' + Date.now();
      } else {
        btn.textContent = 'Switch to overlay';
        overlayImg.src = '';
        overlay.classList.add('hidden');
        thermal.classList.remove('hidden');
        visual.classList.remove('hidden');
      }
    });
  })();
</script>
</body>
</html>
"""


class Camera:
    """Background-thread reader that always exposes the most recent frame.

    Decoupling capture from HTTP response generators means a slow or
    disconnected client can never starve the other stream's pipeline.
    """

    def __init__(self, index, label, transform=None):
        self.index = index
        self.label = label
        self.transform = transform
        self._frame = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop,
                                        name=f"cam-{label}", daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()

    def _open(self):
        for attempt in range(OPEN_RETRIES):
            cap = cv2.VideoCapture(self.index, cv2.CAP_V4L2)
            if cap.isOpened():
                ok, _ = cap.read()
                if ok:
                    print(f"[viewer] {self.label}: /dev/video{self.index} ready")
                    return cap
                cap.release()
            print(f"[viewer] waiting for {self.label} on /dev/video{self.index} "
                  f"(attempt {attempt + 1}/{OPEN_RETRIES})...")
            time.sleep(OPEN_DELAY_S)
        print(f"[viewer] ERROR: could not open {self.label} on /dev/video{self.index}",
              file=sys.stderr)
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
                if self.transform is not None:
                    frame = self.transform(frame)
                with self._lock:
                    self._frame = frame
        finally:
            cap.release()

    def latest(self):
        with self._lock:
            return None if self._frame is None else self._frame.copy()


def thermal_to_color(frame_bgr):
    """v4l2loopback surfaces Y8 as 3-channel BGR with R=G=B=Y; collapse, stretch, colormap."""
    gray = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2GRAY)
    lo, hi = int(gray.min()), int(gray.max())
    if hi > lo:
        gray = ((gray.astype(np.int32) - lo) * 255 // (hi - lo)).astype(np.uint8)
    color = cv2.applyColorMap(gray, cv2.COLORMAP_INFERNO)
    return cv2.resize(color, (THERMAL_DISPLAY_W, THERMAL_DISPLAY_H),
                      interpolation=cv2.INTER_NEAREST)


def fit_visual(frame_bgr):
    if frame_bgr.shape[:2] != (VISUAL_H, VISUAL_W):
        return cv2.resize(frame_bgr, (VISUAL_W, VISUAL_H))
    return frame_bgr


def make_overlay_source(thermal_cam, visual_cam):
    def source():
        t = thermal_cam.latest()
        v = visual_cam.latest()
        if t is None or v is None:
            return None
        if v.shape[:2] != t.shape[:2]:
            v = cv2.resize(v, (t.shape[1], t.shape[0]))
        return cv2.addWeighted(v, 0.55, t, 0.45, 0)
    return source


def mjpeg_generator(source_fn, fps=STREAM_FPS):
    """Yield multipart/x-mixed-replace frames from a callable that returns BGR images."""
    delay = 1.0 / fps
    encode_params = [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY]
    while True:
        frame = source_fn()
        if frame is None:
            time.sleep(0.05)
            continue
        ok, buf = cv2.imencode('.jpg', frame, encode_params)
        if not ok:
            time.sleep(0.05)
            continue
        jpeg = buf.tobytes()
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n'
               b'Content-Length: ' + str(len(jpeg)).encode() + b'\r\n\r\n'
               + jpeg + b'\r\n')
        time.sleep(delay)


def primary_ipv4():
    """Best-effort LAN IP — open a UDP socket to a public address (no traffic sent)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser(description="FLIR One Pro web viewer")
    ap.add_argument("--thermal", type=int, default=1,
                    help="thermal /dev/video index (default 1)")
    ap.add_argument("--visual", type=int, default=2,
                    help="visible /dev/video index (default 2)")
    ap.add_argument("--host", default="0.0.0.0",
                    help="bind address (default 0.0.0.0 = all interfaces)")
    ap.add_argument("--port", type=int, default=5000,
                    help="HTTP port (default 5000)")
    args = ap.parse_args()

    thermal_cam = Camera(args.thermal, "thermal", transform=thermal_to_color)
    visual_cam = Camera(args.visual, "visual", transform=fit_visual)
    thermal_cam.start()
    visual_cam.start()

    app = Flask(__name__)

    @app.route("/")
    def index():
        return render_template_string(INDEX_HTML)

    @app.route("/stream/thermal")
    def stream_thermal():
        return Response(mjpeg_generator(thermal_cam.latest),
                        mimetype="multipart/x-mixed-replace; boundary=frame")

    @app.route("/stream/visual")
    def stream_visual():
        return Response(mjpeg_generator(visual_cam.latest),
                        mimetype="multipart/x-mixed-replace; boundary=frame")

    @app.route("/stream/overlay")
    def stream_overlay():
        return Response(mjpeg_generator(make_overlay_source(thermal_cam, visual_cam)),
                        mimetype="multipart/x-mixed-replace; boundary=frame")

    ip = primary_ipv4()
    print("")
    print("[viewer] FLIR One Pro web viewer ready")
    print(f"[viewer]   local:   http://127.0.0.1:{args.port}")
    print(f"[viewer]   network: http://{ip}:{args.port}")
    print(f"[viewer]   streams: /stream/thermal  /stream/visual  /stream/overlay")
    print("")

    try:
        app.run(host=args.host, port=args.port, threaded=True, debug=False)
    finally:
        thermal_cam.stop()
        visual_cam.stop()


if __name__ == "__main__":
    main()
