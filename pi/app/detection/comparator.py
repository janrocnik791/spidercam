"""
SpiderCam Pi – Frame Comparator

Compares a captured thermal frame against the matching baseline frame and runs
the leak detector on the difference. The alignment + difference algorithm is
adapted from ``program.py`` (SIFT feature match → homography → warp → absdiff →
threshold → morphology), changed to operate on single-channel grayscale Y8
frames instead of colour images.

    compare_frames(baseline_path, current_path, x, y) -> list[LeakAlert]
        one frame vs its baseline at gantry coordinate (x, y)

    compare_pass(current_dir, baseline_dir) -> list[LeakAlert]
        every "{x}_{y}.jpg" in a completed pass vs the baseline pass
"""

import os

import cv2
import numpy as np

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


def _diff_mask(aligned_gray, current_gray, threshold_value=30):
    """Cleaned binary diff mask. Grayscale-adapted ``program.method_detect_diff1``:
    inputs are already single-channel, so the BGR→gray conversion is skipped."""
    raw_diff = cv2.absdiff(aligned_gray, current_gray)
    _, thresh_diff = cv2.threshold(raw_diff, threshold_value, 255, cv2.THRESH_BINARY)
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
    cleaned_diff = cv2.morphologyEx(thresh_diff, cv2.MORPH_OPEN, kernel)
    return cleaned_diff


def compare_frames(baseline_path: str, current_path: str, x: int, y: int) -> list:
    """Compare one captured frame against its baseline → ``list[LeakAlert]``.

    Returns ``[]`` when there is no baseline frame for this cell yet (the first
    pass ever). Alignment is attempted but falls back to an un-warped compare
    when SIFT can't find a homography — common on low-texture thermal frames.
    """
    if not baseline_path or not os.path.exists(baseline_path):
        return []   # first pass ever — nothing to compare against

    baseline = cv2.imread(baseline_path, cv2.IMREAD_GRAYSCALE)
    current = cv2.imread(current_path, cv2.IMREAD_GRAYSCALE)
    if baseline is None or current is None:
        return []

    try:
        aligned = _align_gray(baseline, current)
    except cv2.error:
        aligned = None

    if aligned is None:
        # No reliable homography — compare directly. absdiff needs matching
        # sizes, so resize the baseline to the current frame if they differ.
        if baseline.shape != current.shape:
            baseline = cv2.resize(baseline, (current.shape[1], current.shape[0]))
        aligned = baseline

    diff = _diff_mask(aligned, current, threshold_value=30)
    return leak_detector.check(diff, x, y)


def compare_pass(current_dir: str, baseline_dir: str) -> list:
    """Walk every ``{x}_{y}.jpg`` in a completed pass and compare it to the
    matching baseline frame. Returns the aggregated list of LeakAlerts."""
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
    stem = os.path.splitext(filename)[0]
    x_str, y_str = stem.split("_")
    return int(x_str), int(y_str)
