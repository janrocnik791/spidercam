#!/bin/bash
# ============================================================================
# Spidercam demo launcher
#   1. restarts the waypoint server
#   2. waits until it responds on /demo/movement
#   3. opens the movement interface in the default browser
#
# Passwordless restart needs this one-time sudoers entry (see demo setup notes):
#   pi ALL=(ALL) NOPASSWD: /usr/bin/systemctl restart waypoint-server
# in /etc/sudoers.d/waypoint-server  — otherwise this prompts for a password.
# ============================================================================

URL="http://192.168.253.249:8765/demo/movement"

echo "Starting waypoint server..."
sudo systemctl restart waypoint-server

# Wait until server responds (max 10 seconds)
ok=0
for i in $(seq 1 10); do
    if curl -s "http://localhost:8765/demo/movement" -o /dev/null -w "%{http_code}" | grep -q "200"; then
        echo "Server is up."
        ok=1
        break
    fi
    echo "Waiting... ($i)"
    sleep 1
done

if [ "$ok" -ne 1 ]; then
    echo "Warning: server did not respond after 10s — opening anyway." >&2
fi

echo "Opening interface..."
xdg-open "$URL" >/dev/null 2>&1 || echo "Could not auto-open a browser. Open it manually: $URL"
