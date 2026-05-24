"""
SpiderCam Pi – Leak Detector

Takes a *cleaned binary diff mask* (produced by ``comparator``) for one captured
frame at a given gantry coordinate and decides whether it constitutes a leak
alert. Contour regions above ``config.MIN_ALERT_AREA`` become alerts, and alerts
are de-duplicated per pass by gantry coordinate AND by image location, so the
same hotspot isn't reported on every frame the camera lingers over it (the
image-location check is what handles the fixed camera).

The diff mask is on the Y8 (0–255) intensity scale: the FLIR loopback feed is a
contrast-stretched Y8 image, so the "delta" here is an intensity difference, not
a calibrated °C value (see ``app/config.py`` · FLIR temperature notes).
"""

from dataclasses import dataclass

import cv2

from app.config import MIN_ALERT_AREA, IMAGE_MERGE_DISTANCE

# Two gantry coordinates within this Manhattan distance are treated as the same
# physical zone for de-duplication within a single pass (legacy; relevant for a
# moving rig — a no-op for the fixed-camera time-based scan where cells are far
# apart). The image-location check below is what dedups the fixed-camera case.
_ZONE_MERGE_DISTANCE = 4

# Zones already alerted on during the current pass — by gantry coordinate and by
# bbox centroid in the thermal frame. Module-global so the live capture-loop
# comparison and the end-of-pass batch share the same memory. Both cleared by
# reset_alert_zones() at the start of every pass.
_alerted_zones: list[tuple[int, int]] = []
_alerted_image_zones: list[tuple[int, int]] = []


@dataclass
class LeakAlert:
    x: int            # gantry X at capture (mm, from the ESP32)
    y: int            # gantry Y at capture (mm)
    max_delta: float  # max pixel diff value within the bbox (0–255 scale, Y8)
    bbox: tuple       # (bx, by, bw, bh) of the flagged region within the frame
    diff_path: str = ""  # path to the saved _diff.jpg overlay (set by comparator)


def reset_alert_zones():
    """Clear the per-pass zone memory. Call at the start of each new pass."""
    _alerted_zones.clear()
    _alerted_image_zones.clear()


def check(diff, x, y) -> list[LeakAlert]:
    """Find leak regions in a cleaned diff mask, de-duplicated per pass.

    Contours with area ≥ ``MIN_ALERT_AREA`` are candidate leaks. A candidate is
    suppressed when an already-alerted zone is within ``_ZONE_MERGE_DISTANCE``
    (gantry mm) of ``(x, y)`` OR within ``IMAGE_MERGE_DISTANCE`` (pixels) of an
    already-alerted bbox centroid. The image-location check collapses a fixed-
    camera hotspot — which lands at the same pixels in every frame — to a single
    alert per pass instead of one per cell.
    """
    # findContours returns (contours, hierarchy) on OpenCV 4 and
    # (image, contours, hierarchy) on 3 — handle both like program.py does.
    found = cv2.findContours(diff, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    contours = found[0] if len(found) == 2 else found[1]

    alerts = []
    for c in contours:
        if cv2.contourArea(c) < MIN_ALERT_AREA:
            continue   # too small — sensor noise, ignore

        bx, by, bw, bh = cv2.boundingRect(c)
        cx, cy = bx + bw // 2, by + bh // 2

        if any(abs(x - zx) + abs(y - zy) <= _ZONE_MERGE_DISTANCE
               for zx, zy in _alerted_zones):
            continue   # same gantry zone this pass (moving-rig dedup)
        if any(abs(cx - icx) + abs(cy - icy) <= IMAGE_MERGE_DISTANCE
               for icx, icy in _alerted_image_zones):
            continue   # same image location this pass (fixed-camera hotspot)

        region = diff[by:by + bh, bx:bx + bw]
        max_delta = float(region.max()) if region.size else 0.0

        _alerted_zones.append((x, y))
        _alerted_image_zones.append((cx, cy))
        alerts.append(LeakAlert(x=x, y=y, max_delta=max_delta, bbox=(bx, by, bw, bh)))

    return alerts
