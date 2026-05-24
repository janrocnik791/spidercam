#!/usr/bin/env bash
# Start the Argus backend: ensure the FLIR camera pipeline is up, then launch
# the Flask + SocketIO server that serves the Argus UI and the camera streams.
#
# Correct startup order:
#   1. v4l2loopback module loaded (creates /dev/video1,2,3)
#   2. flirone-v4l2 C driver running (demuxes USB → those loopback devices)
#   3. Argus web server
#
# NOTE: the driver takes a *palette file* as its only argument — there is NO
# "--pro" flag (the Pro is the driver's default target). Passing --pro would be
# treated as a (missing) palette path and abort. If the driver is already
# running we leave it alone: launching a second instance fails with an EBUSY
# assertion because the first still holds the USB device + loopback formats.
set -euo pipefail

PI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVER_DIR="$PI_DIR/flir-one-viewer/driver"
DRIVER="$DRIVER_DIR/flirone-v4l2"
PALETTE="${FLIR_PALETTE:-$DRIVER_DIR/palettes/Iron2.raw}"
PYTHON="${PYTHON:-/usr/bin/python3}"   # system python has cv2/numpy/flask; the
                                       # .venv is for the unused MLX path only.
STARTED_DRIVER=""

echo "=== [argus] FLIR camera driver ==="
if ! lsmod | grep -q '^v4l2loopback'; then
    echo "[argus] loading v4l2loopback (devices=3 video_nr=1,2,3)..."
    sudo modprobe v4l2loopback devices=3 video_nr=1,2,3 \
        exclusive_caps=0,0,0 \
        card_label="FLIR-Thermal,FLIR-Visual,FLIR-Blend"
fi

if ! lsusb | grep -qi '09cb:1996'; then
    echo "[argus] WARNING: FLIR One Pro (09cb:1996) not seen on USB — feeds will" >&2
    echo "[argus]          retry until the camera is plugged in." >&2
fi

if pgrep -f 'flirone-v4l2' >/dev/null; then
    echo "[argus] driver already running — reusing it."
else
    if [[ ! -x "$DRIVER" ]]; then
        echo "[argus] building driver..."; make -C "$DRIVER_DIR"
    fi
    echo "[argus] launching driver (palette: $(basename "$PALETTE"))..."
    "$DRIVER" "$PALETTE" >/tmp/argus-driver.log 2>&1 &
    STARTED_DRIVER=$!
    echo "[argus] driver pid $STARTED_DRIVER (log: /tmp/argus-driver.log)"
    # Only tear down the driver if we were the ones who started it.
    trap 'echo "[argus] stopping driver (pid $STARTED_DRIVER)"; kill "$STARTED_DRIVER" 2>/dev/null || true' EXIT
    sleep 3
fi

echo "=== [argus] web server ==="
# Stop any server already bound to :5000 first. Otherwise this new instance
# can't bind, exits, and the STALE process keeps serving — so re-running this
# script appears to do nothing (e.g. an old UI build keeps showing). A clean
# restart must replace the running server.
EXISTING="$(pgrep -f '[a]pp\.main' || true)"
if [[ -n "$EXISTING" ]]; then
    echo "[argus] stopping existing web server (pid $EXISTING)..."
    kill $EXISTING 2>/dev/null || true
    for _ in $(seq 1 20); do ss -ltn 2>/dev/null | grep -q ':5000' || break; sleep 0.25; done
fi
IP="$(hostname -I 2>/dev/null | awk '{print $1}')"; [[ -z "$IP" ]] && IP="127.0.0.1"
echo "[argus]   open in any browser on the LAN:  http://${IP}:5000"
cd "$PI_DIR"
"$PYTHON" -m app.main
