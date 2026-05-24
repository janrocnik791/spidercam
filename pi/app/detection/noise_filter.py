"""
SpiderCam Pi – Noise Filter

Spatial / temporal smoothing for thermal frames.

``apply`` Gaussian-blurs a single frame to knock down sensor noise without
blurring real thermal features much. ``temporal_average`` averages several
frames captured at the same coordinate (capture-time noise reduction for
sensors with high frame noise).

Note: the current FLIR comparison path (``comparator.compare_frames``) does its
own clean-up via threshold + morphology on the Y8 diff, so this module is the
reusable smoothing primitive rather than a mandatory step in that path.
"""

import cv2
import numpy as np

from app.config import NOISE_FILTER_KERNEL


def apply(frame: np.ndarray) -> np.ndarray:
    """Gaussian-blur ``frame`` with the configured kernel; returns float32.

    The kernel must be odd for ``cv2.GaussianBlur``; bump an even config value
    up by one rather than failing.
    """
    k = NOISE_FILTER_KERNEL
    if k % 2 == 0:
        k += 1
    blurred = cv2.GaussianBlur(frame.astype(np.float32), (k, k), 0)
    return blurred.astype(np.float32)


def temporal_average(frames: list[np.ndarray]) -> np.ndarray:
    """Average multiple frames captured at the same coordinate; returns float32."""
    if not frames:
        raise ValueError("temporal_average requires at least one frame")
    stack = np.stack([f.astype(np.float32) for f in frames], axis=0)
    return stack.mean(axis=0).astype(np.float32)
