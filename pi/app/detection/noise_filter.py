"""
SpiderCam Pi – Noise Filter

Applies spatial smoothing to a thermal frame before comparison.
A Gaussian blur removes sensor noise without blurring real thermal features much.

Claude Code instructions:

    def apply(frame: np.ndarray) -> np.ndarray:
        Use cv2.GaussianBlur with kernel size config.NOISE_FILTER_KERNEL.
        Kernel must be odd (e.g. 3 for MLX90640 at 32×24, 5 for FLIR at 160×120).
        Return smoothed frame as float32.

    Optionally add:
    def temporal_average(frames: list[np.ndarray]) -> np.ndarray:
        Average multiple frames captured at the same coordinate.
        Used in inspection_runner before saving, for sensors with high frame noise.
"""

# import numpy as np
# import cv2
# from app.config import NOISE_FILTER_KERNEL
