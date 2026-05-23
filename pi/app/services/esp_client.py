"""
SpiderCam Pi – ESP32 HTTP Client

Single place for all Pi → ESP32 communication. Nothing else calls ``requests``
directly. The ESP32 drives the 4 cable motors and reports position + state.

Every method raises :class:`ESP32Unreachable` when the controller is offline
(connection refused / timed out / DNS failure) so callers can degrade
gracefully instead of crashing. The ESP32 is frequently *not* connected during
bring-up, so this is the expected path, not an error.

Assumed ESP32 firmware HTTP surface (mirrors the Argus control needs):
    GET  /ping       -> {"status": "ok"}
    POST /move       {"direction","speed"} -> {"x","y","z", ...}
    POST /stop       -> {...}
    POST /home       -> {...}
    GET  /position   -> {"x","y","z"}
    GET  /status     -> {"motors","tension", ...}
    POST /estop      -> {...}
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

    def estop(self):
        """POST /estop — hard halt, motors brake and hold tension."""
        return self._request("POST", "/estop")

    def ping(self):
        """Return True iff the ESP32 responds with {"status": "ok"}."""
        try:
            data = self._request("GET", "/ping")
        except ESP32Unreachable:
            return False
        return data.get("status") == "ok"
