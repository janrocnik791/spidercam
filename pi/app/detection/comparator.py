"""
SpiderCam Pi – Frame Comparator

Compares a captured thermal frame against the matching baseline frame and runs
the leak detector on the difference. The difference algorithm is adapted from
``program.py`` (median denoise → absdiff → threshold → morphology), operating on
single-channel grayscale Y8 frames instead of colour images.

Optional SIFT homography registration (``program``'s alignment step) is gated
behind ``config.ALIGN_FRAMES`` and OFF by default: the camera is fixed so frames
are already aligned, and SIFT on low-texture thermal frames fits spurious warps
that create large false diffs.

    compare_frames(baseline_path, current_path, x, y) -> list[LeakAlert]
        one frame vs its baseline at gantry coordinate (x, y)

    compare_pass(current_dir, baseline_dir) -> list[LeakAlert]
        every "{x}_{y}.jpg" in a completed pass vs the baseline pass
"""

import os

import cv2
import numpy as np

from app import config
from app.detection import leak_detector


def _align_gray(baseline_gray, current_gray):
    """Warp ``baseline_gray`` to align with ``current_gray``; return the warped
    image, or ``None`` when alignment isn't possible.

    Grayscale-adapted ``program.align_image``: the baseline grayscale is used
    both as the SIFT feature image and as the warp source, and the output size
    comes from ``current_gray.shape[:2]`` (single-channel images have no channel
    axis to unpack).
    """
    sift = cv2.SIFT_create()
    kp1, des1 = sift.detectAndCompute(baseline_gray, None)
    kp2, des2 = sift.detectAndCompute(current_gray, None)
    if des1 is None or des2 is None or len(kp1) < 2 or len(kp2) < 2:
        return None

    FLANN_INDEX_KDTREE = 1
    index_params = dict(algorithm=FLANN_INDEX_KDTREE, trees=5)
    search_params = dict(checks=50)
    flann = cv2.FlannBasedMatcher(index_params, search_params)
    matches = flann.knnMatch(des1, des2, k=2)

    # Lowe's ratio test (guard against knnMatch returning <2 neighbours).
    good = []
    for pair in matches:
        if len(pair) != 2:
            continue
        m, n = pair
        if m.distance < 0.7 * n.distance:
            good.append(m)
    if len(good) < 4:
        return None

    src_pts = np.float32([kp1[m.queryIdx].pt for m in good]).reshape(-1, 1, 2)
    dst_pts = np.float32([kp2[m.trainIdx].pt for m in good]).reshape(-1, 1, 2)
    H, _ = cv2.findHomography(src_pts, dst_pts, cv2.RANSAC, 5.0)
    if H is None:
        return None
    height, width = current_gray.shape[:2]
    return cv2.warpPerspective(baseline_gray, H, (width, height))


def _read_temp_sidecar(frame_path):
    """Frame's calibrated (T_min, T_max) °C from its ``.temp`` sidecar (written
    by the inspection runner), or ``None`` when absent/unreadable."""
    sidecar = os.path.splitext(frame_path)[0] + ".temp"
    try:
        with open(sidecar) as f:
            t_lo, t_hi = (float(v) for v in f.read().split()[:2])
        return t_lo, t_hi
    except (OSError, ValueError):
        return None


def _hot_mask(current_gray, temp_range, min_temp_c):
    """Binary mask (uint8 0/255) of pixels hotter than ``min_temp_c``.

    The Y8 frame is a per-frame linear contrast-stretch of [T_min, T_max], so a
    pixel's °C ≈ ``T_min + (Y8/255) * (T_max - T_min)``; invert that to a Y8
    cut-off. Returns ``None`` when the range is degenerate (can't reconstruct),
    an all-set mask when the whole frame is ≥ ``min_temp_c``, or an empty mask
    when nothing reaches it."""
    t_lo, t_hi = temp_range
    if t_hi <= t_lo:
        return None
    thresh_y8 = (min_temp_c - t_lo) / (t_hi - t_lo) * 255.0
    if thresh_y8 <= 0:
        return np.full_like(current_gray, 255)
    if thresh_y8 >= 255:
        return np.zeros_like(current_gray)
    return (current_gray > thresh_y8).astype(np.uint8) * 255


def _apply_hot_gate(diff, current_gray, temp_range, min_temp_c):
    """AND the diff with the hot mask, so a region must be both *changed* and
    *hot* to survive. Skips the gate (returns diff) on a degenerate range."""
    hot = _hot_mask(current_gray, temp_range, min_temp_c)
    return diff if hot is None else cv2.bitwise_and(diff, hot)


def _diff_mask(aligned_gray, current_gray, threshold_value=30):
    """Cleaned binary diff mask. Grayscale-adapted ``program.method_detect_diff1``:
    inputs are already single-channel, so the BGR→gray conversion is skipped.

    Both frames are median-blurred first (``config.NOISE_FILTER_KERNEL``) to kill
    the FLIR's isolated flickering sensor pixels — a few single-pixel spikes
    otherwise survive the threshold and inflate false detections."""
    k = config.NOISE_FILTER_KERNEL
    if k and k >= 3:
        k = k if k % 2 else k + 1            # medianBlur requires an odd kernel
        aligned_gray = cv2.medianBlur(aligned_gray, k)
        current_gray = cv2.medianBlur(current_gray, k)
    raw_diff = cv2.absdiff(aligned_gray, current_gray)
    _, thresh_diff = cv2.threshold(raw_diff, threshold_value, 255, cv2.THRESH_BINARY)
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
    cleaned_diff = cv2.morphologyEx(thresh_diff, cv2.MORPH_OPEN, kernel)
    return cleaned_diff


def _save_frame_views(current_path, baseline_gray, current_gray, diff):
    """Save baseline + diff-overlay JPEGs next to the captured frame
    (``{stem}_baseline.jpg`` / ``{stem}_diff.jpg``) and return the diff-overlay
    path.

    The "current" view is served straight from the ``{stem}.jpg`` frame the
    inspection_runner already saved, so we deliberately don't write a redundant
    ``_current.jpg`` copy here.

    The diff overlay is the current frame colorized with the iron-like
    ``COLORMAP_HOT`` and the changed regions filled bright green, so the history
    detail view can show where the leak is.
    """
    base_dir = os.path.dirname(current_path)
    stem = os.path.splitext(os.path.basename(current_path))[0]   # "{x}_{y}"
    baseline_out = os.path.join(base_dir, f"{stem}_baseline.jpg")
    diff_out = os.path.join(base_dir, f"{stem}_diff.jpg")

    # Baseline: grayscale, saved as-is.
    cv2.imwrite(baseline_out, baseline_gray)

    # Diff overlay: HOT-colorized current + green filled diff contours on top.
    colorized = cv2.applyColorMap(current_gray, cv2.COLORMAP_HOT)
    found = cv2.findContours(diff, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    contours = found[0] if len(found) == 2 else found[1]
    cv2.drawContours(colorized, contours, -1, (0, 255, 0), thickness=cv2.FILLED)
    cv2.imwrite(diff_out, colorized)

    return diff_out


def _detect_absolute(current, current_path, x, y):
    """Absolute hot-spot detection (``config.LEAK_ABSOLUTE_MODE``): flag hot
    regions in the current frame directly — no baseline — so a steadily-hot leak
    is reported on every pass, not only the pass where it changes. Requires the
    frame's calibrated temp range (``.temp`` sidecar); without it (or with the
    gate disabled) nothing is flagged.

    The driver's thin burned-in crosshair/markers sit at Y8≈255 and so fall in
    the hot mask, but they're well under ``MIN_ALERT_AREA`` and are dropped by
    the area filter in ``leak_detector.check`` — a real hotspot is a solid blob.
    """
    temp_range = _read_temp_sidecar(current_path)
    if config.LEAK_TEMP_MIN_C is None or temp_range is None:
        return []

    cur = current
    k = config.NOISE_FILTER_KERNEL
    if k and k >= 3:
        cur = cv2.medianBlur(cur, k if k % 2 else k + 1)   # drop sensor sparkle

    hot = _hot_mask(cur, temp_range, config.LEAK_TEMP_MIN_C)
    if hot is None:
        return []
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
    hot = cv2.morphologyEx(hot, cv2.MORPH_OPEN, kernel)

    alerts = leak_detector.check(hot, x, y)
    if not alerts:
        return alerts
    # No baseline in this mode — reuse the current frame for the "past" view.
    diff_path = _save_frame_views(current_path, current, current, hot)
    for alert in alerts:
        alert.diff_path = diff_path
    return alerts


def compare_frames(baseline_path: str, current_path: str, x: int, y: int) -> list:
    """One captured frame → ``list[LeakAlert]``. Two modes (config):

    * ``LEAK_ABSOLUTE_MODE`` ON  → flag hot regions in the current frame directly
      (no baseline); reliable for a steadily-hot leak.
    * default                    → flag regions that *changed* vs the baseline and
      are also hot. Returns ``[]`` with no baseline yet (first pass ever).

    Either way the area filter + image-space dedup (leak_detector) apply.
    """
    current = cv2.imread(current_path, cv2.IMREAD_GRAYSCALE)
    if current is None:
        return []

    if config.LEAK_ABSOLUTE_MODE:
        return _detect_absolute(current, current_path, x, y)

    # ── change-vs-baseline mode ──────────────────────────────────────────────
    if not baseline_path or not os.path.exists(baseline_path):
        return []   # first pass ever — nothing to compare against
    baseline = cv2.imread(baseline_path, cv2.IMREAD_GRAYSCALE)
    if baseline is None:
        return []
    baseline_original = baseline   # keep the as-loaded baseline for saving

    # Alignment is OFF by default (config.ALIGN_FRAMES): the camera is fixed, so
    # frames are already registered. SIFT homography on low-texture thermal frames
    # fits spurious warps that misalign the static scene and create large false
    # diffs — direct comparison is both correct and far cleaner here. Enable it
    # only for a moving rig that genuinely needs registration.
    aligned = None
    if config.ALIGN_FRAMES:
        try:
            aligned = _align_gray(baseline, current)
        except cv2.error:
            aligned = None

    if aligned is None:
        # Direct compare. absdiff needs matching sizes, so resize the baseline to
        # the current frame if they differ.
        if baseline.shape != current.shape:
            baseline = cv2.resize(baseline, (current.shape[1], current.shape[0]))
        aligned = baseline

    diff = _diff_mask(aligned, current, threshold_value=30)

    # Absolute hot gate: keep only changed pixels that are genuinely hot, so a
    # real leak (heated resistor) flags but cool-scene changes don't. Needs the
    # frame's calibrated temp range (.temp sidecar); skipped if unavailable or
    # disabled (config.LEAK_TEMP_MIN_C is None).
    if config.LEAK_TEMP_MIN_C is not None:
        temp_range = _read_temp_sidecar(current_path)
        if temp_range is not None:
            diff = _apply_hot_gate(diff, current, temp_range, config.LEAK_TEMP_MIN_C)

    alerts = leak_detector.check(diff, x, y)
    if not alerts:
        # No detection in this cell — don't litter the pass with _baseline.jpg
        # / _diff.jpg overlays that nothing references.
        return alerts

    diff_path = _save_frame_views(current_path, baseline_original, current, diff)
    for alert in alerts:
        alert.diff_path = diff_path
    return alerts


def compare_pass(current_dir: str, baseline_dir: str) -> list:
    """Walk every captured frame (``{x}_{y}.jpg`` or ``frame_{n}.jpg``) in a
    completed pass and compare it to the matching baseline frame. Returns the
    aggregated list of LeakAlerts."""
    results = []
    if not current_dir or not os.path.isdir(current_dir):
        return results

    for filename in sorted(os.listdir(current_dir)):
        if not filename.lower().endswith(".jpg"):
            continue
        try:
            x, y = _parse_coord(filename)
        except ValueError:
            continue   # not a coordinate-named frame — skip
        current_path = os.path.join(current_dir, filename)
        baseline_path = os.path.join(baseline_dir, filename) if baseline_dir else None
        results.extend(compare_frames(baseline_path, current_path, x, y))

    return results


def _parse_coord(filename: str) -> tuple[int, int]:
    """Parse a gantry coordinate from a captured frame's filename. Both naming
    schemes inspection_runner produces are accepted:

      * ``{x}_{y}.jpg``   – real ESP32 gantry coordinate (mm).
      * ``frame_{n}.jpg`` – sequential fallback used when the ESP32 is offline;
        the capture index ``n`` is treated as x, with y fixed at 0.

    Raises ``ValueError`` for any other name (e.g. the ``_baseline``/``_diff``
    overlays) so callers can skip it.
    """
    stem = os.path.splitext(filename)[0]
    if stem.startswith("frame_"):
        return int(stem[len("frame_"):]), 0
    x_str, y_str = stem.split("_")
    return int(x_str), int(y_str)
