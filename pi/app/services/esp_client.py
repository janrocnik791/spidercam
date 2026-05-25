"""
SpiderCam Pi – ESP32 HTTP Client

Single place for all Pi → ESP32 communication. Nothing else calls ``requests``
directly. The ESP32 drives the 4 cable motors and reports position + state.

Every method raises :class:`ESP32Unreachable` when the controller is offline
(connection refused / timed out / DNS failure) so callers can degrade
gracefully instead of crashing. The ESP32 is frequently *not* connected during
bring-up, so this is the expected path, not an error.

Assumed ESP32 firmware HTTP surface (mirrors the Argus control needs):
    GET  /ping              -> {"status": "ok"}
    POST /move              {"direction","speed"} -> {"x","y","z", ...}
    POST /stop              -> {...}
    POST /home              -> {...}
    GET  /position          -> {"x","y","z"}
    GET  /status            -> {"motors","tension", ...}
    POST /start_inspection  -> {"status": "ok"}
    POST /pause             -> {"status": "ok"}
    POST /resume            -> {"status": "ok"}
    POST /abort             -> {"status": "ok"}
    POST /estop             -> {"status": "ok"}
    POST /release           -> {"status": "ok"}

The inspection-control commands (start/pause/resume/abort/estop) are
fire-and-forget: the Pi-side scan state machine is the source of truth, so if
the controller is offline the command is logged as a warning and dropped rather
than raised — the UI/scan still advances. The query/motion methods still raise
:class:`ESP32Unreachable` so their callers can react.
"""

import logging

import requests

from app import config

log = logging.getLogger(__name__)

_VALID_DIRECTIONS = {"left", "right", "forward", "backward", "up", "down"}


class ESP32Unreachable(Exception):
    """Raised when the ESP32 cannot be reached (offline / timeout / DNS)."""


class ESP32Client:
    def __init__(self, base_url=None, timeout=None):
        self.base_url = base_url or config.ESP32_BASE_URL
        self.timeout = timeout or config.ESP32_TIMEOUT

    # ── low-level helpers ────────────────────────────────────────────────────────
    def _request(self, method, path, **kwargs):
        url = f"{self.base_url}{path}"
        kwargs.setdefault("timeout", self.timeout)
        log.debug("%s %s %s", method, url, kwargs.get("json"))
        try:
            resp = requests.request(method, url, **kwargs)
            resp.raise_for_status()
        except (requests.exceptions.ConnectionError,
                requests.exceptions.Timeout) as exc:
            raise ESP32Unreachable(str(exc)) from exc
        except requests.exceptions.RequestException as exc:
            # HTTP error / bad URL / etc. — treat as unreachable for the UI.
            raise ESP32Unreachable(str(exc)) from exc
        try:
            return resp.json()
        except ValueError:
            return {}

    def _fire_and_forget(self, path, label):
        """POST an inspection-control command, swallowing unreachability.

        Returns True if the ESP32 acknowledged, False if it was offline (logged
        as a warning). These commands must never block or crash the Pi-side scan
        state machine — the scan/UI updates regardless of controller status."""
        try:
            self._request("POST", path)
            return True
        except ESP32Unreachable as exc:
            log.warning("ESP32 %s command not delivered (POST %s): %s", label, path, exc)
            return False

    # ── public API ───────────────────────────────────────────────────────────────
    def send_command(self, direction, speed=None):
        """POST /move. Returns the ESP32 response (includes updated x/y/z)."""
        if direction not in _VALID_DIRECTIONS:
            raise ValueError(f"invalid direction: {direction!r}")
        payload = {"direction": direction}
        if speed is not None:
            payload["speed"] = int(speed)
        return self._request("POST", "/move", json=payload)

    def stop(self):
        """POST /stop — halt motion (graceful)."""
        return self._request("POST", "/stop")

    def goto_home(self):
        """POST /home — move to the origin."""
        return self._request("POST", "/home")

    def get_position(self):
        """GET /position -> {x, y, z} in mm."""
        return self._request("GET", "/position")

    def get_status(self):
        """GET /status -> motor state, cable tension, etc."""
        return self._request("GET", "/status")

    # ── inspection control (fire-and-forget) ──────────────────────────────────────
    def start_inspection(self):
        """POST /start_inspection — begin the autonomous pass on the controller."""
        return self._fire_and_forget("/start_inspection", "start")

    def pause(self):
        """POST /pause — pause the autonomous pass."""
        return self._fire_and_forget("/pause", "pause")

    def resume(self):
        """POST /resume — resume the autonomous pass."""
        return self._fire_and_forget("/resume", "resume")

    def abort(self):
        """POST /abort — abort the current pass."""
        return self._fire_and_forget("/abort", "abort")

    def estop(self):
        """POST /estop — hard halt, motors brake and hold tension. Fire-and-forget:
        returns True if delivered, False if the controller was offline (the Pi
        still latches E-stop locally regardless)."""
        return self._fire_and_forget("/estop", "ESTOP")

    def release(self):
        """POST /release — clear a latched E-stop on the controller."""
        return self._fire_and_forget("/release", "release")

    def ping(self):
        """Return True iff the ESP32 responds with {"status": "ok"}."""
        try:
            data = self._request("GET", "/ping")
        except ESP32Unreachable:
            return False
        return data.get("status") == "ok"
