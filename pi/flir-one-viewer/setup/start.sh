#!/usr/bin/env bash
# Launch v4l2loopback, the FLIR One Pro USB driver, and the Python viewer.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DRIVER="$ROOT/driver/flirone-v4l2"
PALETTE="${FLIR_PALETTE:-$ROOT/driver/palettes/Iron2.raw}"

if [[ ! -x "$DRIVER" ]]; then
    echo "[start] Driver not built — running: make -C driver"
    make -C driver
fi

if [[ ! -f "$PALETTE" ]]; then
    echo "[start] Palette not found: $PALETTE" >&2
    exit 1
fi

echo "[start] Ensuring v4l2loopback is loaded (devices=3 video_nr=1,2,3)..."
if ! lsmod | grep -q '^v4l2loopback'; then
    sudo modprobe v4l2loopback devices=3 video_nr=1,2,3 \
        exclusive_caps=0,0,0 \
        card_label="FLIR-Thermal,FLIR-Visual,FLIR-Blend"
fi

if ! lsusb | grep -qi '09cb:1996'; then
    echo "[start] WARNING: FLIR One Pro (09cb:1996) not seen on USB." >&2
    echo "[start]          Plug the camera in and re-run." >&2
fi

echo "[start] Launching driver (palette: $(basename "$PALETTE"))..."
"$DRIVER" "$PALETTE" &
DRIVER_PID=$!
trap 'echo "[start] Stopping driver (pid $DRIVER_PID)"; kill $DRIVER_PID 2>/dev/null || true' EXIT

# Give the camera ~3s to enumerate and start streaming before the viewer opens.
sleep 3

PORT="${FLIR_PORT:-5000}"
IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
[[ -z "$IP" ]] && IP="127.0.0.1"

echo "[start] Launching web viewer on port $PORT..."
echo "[start]   open in any browser on the LAN:  http://${IP}:${PORT}"
python3 viewer/viewer.py --port "$PORT" "$@"
