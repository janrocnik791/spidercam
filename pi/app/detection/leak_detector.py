"""
SpiderCam Pi – Leak Detector

Takes a diff array (|current - baseline|) at a given coordinate and decides
whether it constitutes a leak alert.

Claude Code instructions:

    @dataclass
    class LeakAlert:
        x: int                  # Gantry X position (step counts)
        y: int                  # Gantry Y position (step counts)
        max_delta: float        # Largest temperature difference in the frame (°C)
        mean_delta: float       # Mean temp difference across flagged pixels
        affected_pixels: int    # Count of pixels above threshold
        diff_image: np.ndarray  # The diff array for rendering as heatmap

    def check(diff: np.ndarray, x: int, y: int) -> list[LeakAlert]:
        mask = diff > config.LEAK_THRESHOLD
        if mask.sum() < config.MIN_ALERT_AREA:
            return []   # Not enough pixels — sensor noise, ignore

        return [LeakAlert(
            x=x, y=y,
            max_delta=float(diff[mask].max()),
            mean_delta=float(diff[mask].mean()),
            affected_pixels=int(mask.sum()),
            diff_image=diff,
        )]

    def alerts_to_json(alerts: list[LeakAlert]) -> list[dict]:
        Serialise for sending to the browser via SocketIO or REST.
        Convert diff_image to a base64 PNG for inline rendering.
"""

# from dataclasses import dataclass
# import numpy as np
# import cv2, base64
# from app.config import LEAK_THRESHOLD, MIN_ALERT_AREA
