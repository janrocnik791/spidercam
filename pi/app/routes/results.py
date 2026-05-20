"""
SpiderCam Pi – Results Routes

Endpoints:
    GET /api/results/latest
        Returns the list of LeakAlerts from the most recent comparison.
        Response: {"inspection_id": str, "alerts": [ <LeakAlert JSON>, ... ]}
        Each alert includes a base64-encoded diff heatmap PNG for rendering.

    GET /api/results/history
        Returns a list of past inspection IDs from inspections/ directory.
        Used to browse historical passes.

    POST /api/results/confirm-baseline
        Body: {"inspection_id": "<timestamp>"}
        Updates the baseline symlink to point to this inspection.
        Returns {"ok": true}

Claude Code instructions:
    - Cache the latest results in memory (set by InspectionRunner after each pass)
    - confirm-baseline updates baselines/latest symlink: os.symlink(src, dst)
      Handle Windows path differences if testing locally (symlinks need admin on Windows).
"""

from flask import Blueprint

results_bp = Blueprint("results", __name__)
