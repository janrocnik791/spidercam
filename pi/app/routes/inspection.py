"""
SpiderCam Pi – Inspection Routes

Endpoints:
    POST /api/inspection/start
        Calls InspectionRunner.start()
        Returns {"ok": true, "inspection_id": "<timestamp>"}

    POST /api/inspection/stop
        Calls InspectionRunner.stop()
        Returns {"ok": true}

    GET  /api/inspection/status
        Returns {"state": "idle"|"running", "inspection_id": str|null,
                 "frames_captured": int}

Claude Code instructions:
    - InspectionRunner is a singleton (one instance shared across requests)
    - Attach it to Flask's app context so it persists between requests
    - Return 409 if start is called while already running
"""

from flask import Blueprint

inspection_bp = Blueprint("inspection", __name__)
