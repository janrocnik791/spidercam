# Wiring Notes

## ESP32 → Motor Drivers
Fill in once you've selected a driver module (e.g. A4988, DRV8825, TMC2209).

| ESP32 GPIO | Driver pin | Notes |
|---|---|---|
| 14 | X STEP | MOTOR_X_STEP_PIN in config.h |
| 12 | X DIR  | MOTOR_X_DIR_PIN |
| 27 | Y STEP | MOTOR_Y_STEP_PIN |
| 26 | Y DIR  | MOTOR_Y_DIR_PIN |
| 2  | Onboard LED | Blinks on WiFi connect + on /ping |

## Raspberry Pi 5 → MLX90640
The MLX90640 uses I²C.

| Pi pin | MLX90640 pin |
|---|---|
| 3.3V (pin 1) | VCC |
| GND (pin 6)  | GND |
| SDA (pin 3, GPIO 2) | SDA |
| SCL (pin 5, GPIO 3) | SCL |

Enable I²C on Pi: `sudo raspi-config` → Interface Options → I2C → Enable.

Verify sensor is detected: `i2cdetect -y 1` should show `0x33`.

## Raspberry Pi 5 → FLIR One Pro
USB-C connection only. No GPIO needed.
The FLIR One Pro appears as two video devices — the thermal one is typically `/dev/video2`.
Run `v4l2-ctl --list-devices` to confirm.

## Power
- ESP32 dev board: powered via USB from any 5V supply or directly from Pi USB port
- Motor drivers: need separate 12V (or voltage appropriate for your steppers)
- Pi 5: requires 5V 5A USB-C supply (official Pi supply recommended)
