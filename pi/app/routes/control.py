"""
SpiderCam Pi – Control Routes

Manual movement commands from the browser.

Endpoints:
    POST /api/move
        Body: {"direction": "left"|"right"|"forward"|"backward"}
        Calls esp_client.move(direction)
        Returns the ESP32 response (includes updated x, y position)

    POST /api/stop
        Calls esp_client.stop()
        Returns {"ok": true}

    GET /api/ping
        Calls esp_client.ping()
        Returns {"ok": true|false}
        Used by the UI on load to show connection status (green/red indicator).

Claude Code instructions:
    - Blueprint name: control_bp, url_prefix set in __init__.py to /api
    - Validate direction value; return 400 if invalid
    - Return 503 if ESP32 is unreachable (catch connection exceptions)
"""

from flask import Blueprint

control_bp = Blueprint("control", __name__)
