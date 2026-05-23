#!/usr/bin/env bash
# One-time setup for the FLIR One Pro viewer on Raspberry Pi OS (Bookworm/Bullseye).
# Idempotent — safe to re-run.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "=== [1/5] Installing system dependencies ==="
sudo apt update
sudo apt install -y \
    libusb-1.0-0-dev \
    libjpeg-dev \
    build-essential \
    v4l2loopback-dkms \
    v4l2loopback-utils \
    python3-pip \
    python3-numpy \
    python3-opencv \
    python3-flask

echo "=== [2/5] Building flirone-v4l2 driver ==="
make -C driver clean
make -C driver

echo "=== [3/5] Installing udev rule for FLIR One Pro USB access ==="
sudo cp setup/77-flirone-lusb.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "=== [4/5] Adding $USER to the 'video' group ==="
sudo usermod -aG video "$USER"

echo "=== [5/5] Configuring v4l2loopback to auto-load on boot ==="
echo "v4l2loopback" | sudo tee /etc/modules-load.d/v4l2loopback.conf >/dev/null
sudo tee /etc/modprobe.d/v4l2loopback.conf >/dev/null <<'EOF'
options v4l2loopback devices=3 video_nr=1,2,3 exclusive_caps=0,0,0 card_label="FLIR-Thermal","FLIR-Visual","FLIR-Blend"
EOF

cat <<EOF

=== Setup complete ===

IMPORTANT: log out and back in (or reboot) so the 'video' group membership
and udev rule take effect. After that:

    bash setup/start.sh

EOF
