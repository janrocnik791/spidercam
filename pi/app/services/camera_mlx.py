"""
SpiderCam Pi – MLX90640 Camera (32×24, ~4 fps via I²C)
"""

import numpy as np
import board
import busio
import adafruit_mlx90640

from .camera_base import CameraBase
from app.config import MLX_FRAME_RATE

# Maps Hz rate from config to the 3-bit register value the sensor expects.
# RefreshRate enum values: 0=0.5Hz 1=1Hz 2=2Hz 3=4Hz 4=8Hz 5=16Hz 6=32Hz 7=64Hz
_RATE_REG = {0.5: 0, 1: 1, 2: 2, 4: 3, 8: 4, 16: 5, 32: 6, 64: 7}


class MLX90640Camera(CameraBase):

    def __init__(self):
        i2c = busio.I2C(board.SCL, board.SDA)
        self._mlx = adafruit_mlx90640.MLX90640(i2c)
        self._mlx.refresh_rate = _RATE_REG[MLX_FRAME_RATE]
        self._buf = [0.0] * 768

    def get_frame(self) -> np.ndarray:
        self._mlx.getFrame(self._buf)
        return np.array(self._buf, dtype=np.float32).reshape((24, 32))

    def get_resolution(self) -> tuple[int, int]:
        return (32, 24)
