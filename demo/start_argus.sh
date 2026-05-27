#!/bin/bash
# ============================================================================
# Argus thermal-imaging demo launcher
#   1. starts (restarts) the Argus Flask/SocketIO server
#   2. waits until it responds on :5000
#   3. opens the Argus interface in the default browser
#
# This mirrors the project's canonical launcher exactly by invoking
#   pi/start_argus.sh
# which brings up the FLIR camera pipeline (v4l2loopback + flirone-v4l2 driver)
# and then runs `python3 -m app.main` (Flask + SocketIO on :5000). That script
# already stops/replaces any server bound to :5000, so this is a clean restart.
#
# It runs in the FOREGROUND there, so we launch it detached in the background and
# then poll for readiness. No systemd service / sudoers entry is needed for the
# web server; the only privileged step inside pi/start_argus.sh is a conditional
# `sudo modprobe v4l2loopback`, skipped when the module is already loaded.
# ============================================================================

PI_START="/home/pi/projects/spidercam/pi/start_argus.sh"
PORT=5000
URL="http://192.168.253.249:${PORT}/"

echo "Starting Argus thermal server..."
# Detached + stdin from /dev/null so it keeps running after this script exits and
# never blocks on an interactive prompt. Output goes to a log for troubleshooting.
nohup "$PI_START" </dev/null >/tmp/argus-start.log 2>&1 &

# Wait until the server responds. Argus also has to bring the camera up, so allow
# a bit longer than the movement server (which is near-instant).
ok=0
for i in $(seq 1 20); do
    if curl -s "http://localhost:${PORT}/" -o /dev/null -w "%{http_code}" | grep -q "200"; then
        echo "Argus server is up."
        ok=1
        break
    fi
    echo "Waiting... ($i)"
    sleep 1
done

if [ "$ok" -ne 1 ]; then
    echo "Warning: Argus did not respond after 20s — opening anyway (see /tmp/argus-start.log)." >&2
fi

echo "Opening Argus interface..."
xdg-open "$URL" >/dev/null 2>&1 || echo "Could not auto-open a browser. Open it manually: $URL"
