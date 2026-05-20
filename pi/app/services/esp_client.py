"""
SpiderCam Pi – ESP32 HTTP Client

Single place for all Pi → ESP32 communication.
All other code imports from here; nothing else calls `requests` directly.
"""

import logging

import requests

from app.config import ESP32_BASE_URL, ESP32_TIMEOUT

log = logging.getLogger(__name__)


def ping() -> bool:
    """GET /ping. Returns True if ESP32 responds with {"status": "ok"}."""
    url = f"{ESP32_BASE_URL}/ping"
    log.debug("GET %s", url)
    resp = requests.get(url, timeout=ESP32_TIMEOUT)
    resp.raise_for_status()
    data = resp.json()
    log.debug("response: %s", data)
    return data.get("status") == "ok"
