#!/usr/bin/env python3
# =============================================================================
# SpiderCam — waypoint_server
#
# A tiny stdlib-only HTTP server that owns the inspection-route waypoint file on
# the Raspberry Pi. The ESP32 path_marker firmware cannot write to the Pi's disk
# directly, so on SAVE / DELETE LAST it POSTs here and this server edits the file.
#
# Endpoints (all JSON):
#   POST /waypoint/save          body {"M1":105.3,"M2":98.7,"M3":112.1,"M4":101.5}
#                                optional real-world coords: ,"X":45.2,"Y":80.1,"Z":48.3
#                                appends  M1:105.3,M2:98.7,M3:112.1,M4:101.5[,X:..,Y:..,Z:..]
#                                -> {"status":"ok","total":<n>}
#   POST /waypoint/delete_last   (no body) removes the last line
#                                -> {"status":"ok","total":<n>}
#   POST /waypoint/reset         (no body) clears the file (used by the ESP32 RESET)
#                                -> {"status":"ok","total":0}
#   GET  /waypoint/list          -> {"waypoints":[{"M1":..,..,"M4":..[,"X":..,"Y":..,"Z":..]}, ...]}
#
#   POST /corners/save           body [{"index":0,"x":67.5,"y":18.4,"z":48.2,"set":true}, ...×4]
#                                overwrites corners.txt (4 lines, always) atomically
#                                -> {"status":"ok"}
#   GET  /corners/load           -> [{"index":0,"x":..,"y":..,"z":..,"set":..}, ...×4]
#
# The path_marker_xyz firmware keeps the four taught perimeter corners in ESP32 RAM
# (lost on reboot), so it POSTs the full set here on every save/clear and reloads
# them on boot — letting corners survive page refreshes and ESP32 reboots.
#
# Writes are atomic: the whole file is rewritten to a temp file in the same dir,
# fsync'd, then os.replace()'d over the target — so a power cut mid-write can never
# leave a half-written or corrupted waypoints.txt.
#
# Run (foreground):
#   python3 /home/pi/projects/spidercam/waypoint_server/waypoint_server.py
#
# Run (background, survives logout, logs to nohup.out):
#   nohup python3 /home/pi/projects/spidercam/waypoint_server/waypoint_server.py &
# =============================================================================

import json
import mimetypes
import os
import tempfile
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# ── Configuration ────────────────────────────────────────────────────────────
# Shared over HTTP by both firmwares (path_marker_by_cable_length and
# path_marker_xyz). The physical file lives here, next to the server that owns it,
# so renaming/adding firmware sketch folders never affects it.
WAYPOINTS_FILE = "/home/pi/projects/spidercam/waypoint_server/waypoints.txt"
# Perimeter corners live alongside the firmware sketch that owns them, so they sit
# next to path_marker_xyz.ino exactly where the task expects them.
CORNERS_FILE = "/home/pi/projects/spidercam/esp32/src/path_marker_xyz/corners.txt"
HOST = "0.0.0.0"      # listen on all interfaces so the ESP32 (and a browser) can reach it
PORT = 8765           # checked free — :5000 (Argus backend) is the only nearby port in use

# ── Demo interfaces — served by THIS server, separate from Argus ─────────────
# Standalone demo UIs live under one self-describing folder (spidercam/demo/) so
# it's obvious what belongs to the demo vs the existing system. This server — NOT
# Argus's Flask app — serves the movement interface at /demo/movement, and the
# thermal demo assets under /demo/thermal/* (reserved; populated when images exist).
DEMO_DIR        = "/home/pi/projects/spidercam/demo"
MOVEMENT_HTML   = os.path.join(DEMO_DIR, "movement", "movement.html")
THERMAL_SEQ_DIR = os.path.join(DEMO_DIR, "thermal", "sequence")
BASELINE_IMG    = os.path.join(DEMO_DIR, "thermal", "baseline", "thermal_baseline.jpg")
CURRENT_IMG     = os.path.join(DEMO_DIR, "thermal", "current", "thermal_current.jpg")
IMG_EXTS        = (".jpg", ".jpeg", ".png")


def log(msg):
    """Timestamped line to stdout so saves/deletes are easy to monitor."""
    print("[%s] %s" % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), msg), flush=True)


# ── Waypoint file helpers ────────────────────────────────────────────────────
def read_lines():
    """Return the non-empty waypoint lines, or [] if the file is missing/empty."""
    if not os.path.exists(WAYPOINTS_FILE):
        return []
    with open(WAYPOINTS_FILE, "r") as f:
        return [ln.strip() for ln in f if ln.strip()]


def atomic_write(path, lines):
    """Rewrite `path` from `lines` atomically (temp file + fsync + os.replace)."""
    directory = os.path.dirname(path)
    os.makedirs(directory, exist_ok=True)
    content = ("\n".join(lines) + "\n") if lines else ""
    fd, tmp = tempfile.mkstemp(dir=directory, prefix="." + os.path.basename(path) + "_", suffix=".tmp")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(content)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)   # atomic rename on the same filesystem
    except Exception:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise


def format_line(data):
    """Build 'M1:105.3,M2:98.7,M3:112.1,M4:101.5[,X:45.2,Y:80.1,Z:48.3]'.

    M1..M4 are required (numeric). X, Y, Z are optional real-world coordinates —
    the XYZ firmware sends them, the cable-length firmware does not; when present
    they are appended. Raises ValueError if any of M1..M4 is missing/non-numeric.
    """
    try:
        line = "M1:{:.1f},M2:{:.1f},M3:{:.1f},M4:{:.1f}".format(
            float(data["M1"]), float(data["M2"]), float(data["M3"]), float(data["M4"]))
    except (KeyError, TypeError, ValueError):
        raise ValueError("body must contain numeric M1, M2, M3, M4")
    for k in ("X", "Y", "Z"):
        if data.get(k) is not None:
            try:
                line += ",{}:{:.1f}".format(k, float(data[k]))
            except (TypeError, ValueError):
                raise ValueError("%s must be numeric if provided" % k)
    return line


def parse_line(line):
    """Parse 'M1:105.3,M2:98.7,...' into {'M1':105.3, ...}; skips junk fields."""
    out = {}
    for part in line.split(","):
        if ":" not in part:
            continue
        key, _, val = part.partition(":")
        try:
            out[key.strip()] = float(val)
        except ValueError:
            pass
    return out


# ── Operations (return the JSON-able response dict) ──────────────────────────
def op_save(data):
    line = format_line(data)
    lines = read_lines()
    lines.append(line)
    atomic_write(WAYPOINTS_FILE, lines)
    log("SAVE   %s  -> total=%d" % (line, len(lines)))
    return {"status": "ok", "total": len(lines)}


def op_delete_last():
    lines = read_lines()
    if lines:
        removed = lines.pop()
        atomic_write(WAYPOINTS_FILE, lines)
        log("DELETE %s  -> total=%d" % (removed, len(lines)))
    else:
        log("DELETE (no waypoints to remove) -> total=0")
    return {"status": "ok", "total": len(lines)}


def op_list():
    return {"waypoints": [parse_line(ln) for ln in read_lines()]}


def op_reset():
    atomic_write(WAYPOINTS_FILE, [])  # overwrite with an empty file
    log("RESET  cleared all waypoints -> total=0")
    return {"status": "ok", "total": 0}


# ── Corner file helpers (perimeter corners owned by path_marker_xyz) ──────────
# corners.txt is always exactly 4 lines, one per corner slot:
#   C1:X:67.5,Y:18.4,Z:48.2,set:true
# C<n> is a human-readable label only; the slot is decided by line order (line 1 =
# Corner 1 = index 0). An unset corner is "C<n>:X:0.0,Y:0.0,Z:0.0,set:false".
def format_corner_line(index, corner):
    """Build 'C1:X:67.5,Y:18.4,Z:48.2,set:true' for slot `index` (0-based)."""
    return "C%d:X:%.1f,Y:%.1f,Z:%.1f,set:%s" % (
        index + 1,
        float(corner.get("x", 0.0)), float(corner.get("y", 0.0)), float(corner.get("z", 0.0)),
        "true" if corner.get("set") else "false")


def parse_corner_line(line, index):
    """Parse one corner line into {'index':i,'x':..,'y':..,'z':..,'set':bool}.

    Tolerant of key order/whitespace; the leading 'C<n>:' label is dropped. Missing
    or junk fields fall back to 0.0 / set:false so a corrupt line never crashes load.
    """
    corner = {"index": index, "x": 0.0, "y": 0.0, "z": 0.0, "set": False}
    # Drop the 'C<n>:' label (split on the FIRST colon only — 'set:true' has one too).
    body = line.split(":", 1)[1] if (line[:1] == "C" and ":" in line) else line
    for part in body.split(","):
        key, _, val = part.partition(":")
        key, val = key.strip().lower(), val.strip()
        if key == "set":
            corner["set"] = (val.lower() == "true")
        elif key in ("x", "y", "z"):
            try:
                corner[key] = float(val)
            except ValueError:
                pass
    return corner


def load_corners():
    """Return all 4 corners as a list of dicts; missing file/lines -> unset defaults."""
    lines = []
    if os.path.exists(CORNERS_FILE):
        with open(CORNERS_FILE, "r") as f:
            lines = [ln.strip() for ln in f if ln.strip()]
    return [parse_corner_line(lines[i], i) if i < len(lines)
            else {"index": i, "x": 0.0, "y": 0.0, "z": 0.0, "set": False}
            for i in range(4)]


def op_corners_load():
    return load_corners()


def op_corners_save(data):
    """Overwrite corners.txt with all 4 corners from the JSON array `data`."""
    if not isinstance(data, list):
        raise ValueError("body must be a JSON array of corners")
    slots = [{"x": 0.0, "y": 0.0, "z": 0.0, "set": False} for _ in range(4)]
    for c in data:
        if not isinstance(c, dict):
            raise ValueError("each corner must be an object")
        try:
            i = int(c["index"])
        except (KeyError, TypeError, ValueError):
            raise ValueError("each corner needs an integer 'index'")
        if not (0 <= i < 4):
            raise ValueError("corner index out of range 0..3")
        try:
            slots[i] = {"x": float(c.get("x", 0.0)), "y": float(c.get("y", 0.0)),
                        "z": float(c.get("z", 0.0)), "set": bool(c.get("set", False))}
        except (TypeError, ValueError):
            raise ValueError("x, y, z must be numeric")
    lines = [format_corner_line(i, slots[i]) for i in range(4)]
    atomic_write(CORNERS_FILE, lines)
    set_count = sum(1 for s in slots if s["set"])
    log("CORNERS save -> %d/4 set" % set_count)
    return {"status": "ok"}


# ── Demo thermal-asset helpers ───────────────────────────────────────────────
def op_thermal_sequence():
    """Sorted list of frame image filenames in thermal_sequence/ (empty if none).

    Non-image files (e.g. the placeholder .txt) are ignored so the demo only ever
    sees real frames; the demo builds /demo/thermal/frame/<name> URLs from this.
    """
    try:
        return sorted(f for f in os.listdir(THERMAL_SEQ_DIR)
                      if f.lower().endswith(IMG_EXTS))
    except OSError:
        return []


# ── HTTP handler ─────────────────────────────────────────────────────────────
class Handler(BaseHTTPRequestHandler):
    server_version = "SpiderCamWaypoints/1.0"

    def _send(self, code, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        # Permissive CORS so a browser could also call this directly if ever needed.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        if not raw:
            return {}
        return json.loads(raw.decode("utf-8"))

    def _send_file(self, path, content_type=None):
        """Send a file's bytes with CORS, or 404 JSON if it's missing/unreadable.

        Cache-Control: no-store so the operator can swap the demo HTML or thermal
        images on disk and a browser refresh always picks up the new bytes.
        """
        try:
            with open(path, "rb") as f:
                body = f.read()
        except OSError:
            self._send(404, {"error": "not_found", "path": self.path})
            return
        if content_type is None:
            content_type = mimetypes.guess_type(path)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _serve_frame(self, path):
        """Serve thermal_sequence/<filename>; basename() blocks path traversal."""
        name = os.path.basename(path[len("/demo/thermal/frame/"):])
        if not name:
            self._send(404, {"error": "not_found", "path": self.path})
            return
        self._send_file(os.path.join(THERMAL_SEQ_DIR, name))

    def do_OPTIONS(self):
        self._send(204, {})

    def do_GET(self):
        path = self.path.split("?", 1)[0]        # ignore cache-buster / query args
        if path == "/waypoint/list":
            self._send(200, op_list())
        elif path == "/corners/load":
            self._send(200, op_corners_load())
        elif path in ("/demo/movement", "/demo/movement/"):
            self._send_file(MOVEMENT_HTML, "text/html; charset=utf-8")
        elif path == "/demo/thermal/sequence":
            self._send(200, op_thermal_sequence())
        elif path == "/demo/thermal/baseline":
            self._send_file(BASELINE_IMG)
        elif path == "/demo/thermal/current":
            self._send_file(CURRENT_IMG)
        elif path.startswith("/demo/thermal/frame/"):
            self._serve_frame(path)
        else:
            self._send(404, {"error": "not_found", "path": self.path})

    def do_POST(self):
        try:
            if self.path == "/waypoint/save":
                self._send(200, op_save(self._read_json()))
            elif self.path == "/waypoint/delete_last":
                self._send(200, op_delete_last())
            elif self.path == "/waypoint/reset":
                self._send(200, op_reset())
            elif self.path == "/corners/save":
                self._send(200, op_corners_save(self._read_json()))
            else:
                self._send(404, {"error": "not_found", "path": self.path})
        except ValueError as e:
            log("400 bad request on %s: %s" % (self.path, e))
            self._send(400, {"error": str(e)})
        except Exception as e:                       # never crash the server on one bad request
            log("500 error on %s: %s" % (self.path, e))
            self._send(500, {"error": "server_error"})

    def log_message(self, *args):
        """Silence the default per-request stderr noise; we log saves/deletes explicitly."""
        pass


def main():
    # Create the files on startup if they don't exist (empty route, 4 unset corners).
    if not os.path.exists(WAYPOINTS_FILE):
        atomic_write(WAYPOINTS_FILE, [])
        log("created empty %s" % WAYPOINTS_FILE)
    if not os.path.exists(CORNERS_FILE):
        atomic_write(CORNERS_FILE, [format_corner_line(i, {}) for i in range(4)])
        log("created %s (4 unset corners)" % CORNERS_FILE)

    server = ThreadingHTTPServer((HOST, PORT), Handler)
    server.daemon_threads = True
    log("waypoint_server listening on http://%s:%d  (file: %s, %d waypoints)"
        % (HOST, PORT, WAYPOINTS_FILE, len(read_lines())))
    log("movement demo served at /demo/movement  (dir: %s, %d thermal frame(s))"
        % (DEMO_DIR, len(op_thermal_sequence())))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("shutting down")
        server.server_close()


if __name__ == "__main__":
    main()
