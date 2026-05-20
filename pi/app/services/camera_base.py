"""
SpiderCam Pi – Camera Abstract Base Class
"""

from abc import ABC, abstractmethod
import numpy as np


class CameraBase(ABC):

    @abstractmethod
    def get_frame(self) -> np.ndarray:
        """Capture one frame. Returns (H, W) float32 array of temperatures in Celsius."""

    @abstractmethod
    def get_resolution(self) -> tuple[int, int]:
        """Returns (width, height)."""


def get_camera() -> CameraBase:
    from app.config import CAMERA_TYPE
    if CAMERA_TYPE == "mlx":
        from .camera_mlx import MLX90640Camera
        return MLX90640Camera()
    elif CAMERA_TYPE == "flir":
        from .camera_flir import FLIROneProCamera
        return FLIROneProCamera()
    else:
        raise ValueError(f"Unknown CAMERA_TYPE: {CAMERA_TYPE}")
