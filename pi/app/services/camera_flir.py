"""
SpiderCam Pi – FLIR One Pro Camera (160×120, ~8.7 fps via USB)

The FLIR One Pro presents as two USB video devices: one RGB, one thermal (MSX).
We want the thermal device only.

Claude Code instructions:
  - Use OpenCV (cv2) to capture from the thermal video device
  - Device path from config.FLIR_DEVICE (e.g. "/dev/video0")
  - FLIR One Pro outputs a 16-bit greyscale image where pixel value ≈ temp * 100
    Convert to Celsius: frame_celsius = raw_frame / 100.0 - 273.15
    (exact formula may need calibration — document it here)
  - get_frame():
      ret, raw = cap.read()
      return (raw.astype(np.float32) / 100.0) - 273.15
  - get_resolution(): return (160, 120)

TODO: verify exact pixel→temperature formula once FLIR One Pro arrives.
"""

# import numpy as np
# import cv2
# from .camera_base import CameraBase
# from app.config import FLIR_DEVICE

# class FLIROneProCamera(CameraBase):
#     def __init__(self): ...
#     def get_frame(self) -> np.ndarray: ...
#     def get_resolution(self) -> tuple[int, int]: return (160, 120)
