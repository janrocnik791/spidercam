# ESP32 HTTP API Contract

This is the binding contract between Pi and ESP32.
Both sides must agree on these exactly — if you change one, update the other.

## Base URL
`http://<ESP32_IP>:80`

---

## GET /ping
Health check. Call this first on startup to verify the ESP32 is reachable.

**Response 200**
```json
{"status": "ok"}
```

---

## POST /move
Move the gantry one step increment in a direction.

**Request body**
```json
{"direction": "left" | "right" | "forward" | "backward"}
```

**Response 200**
```json
{"ok": true, "x": 120, "y": 45}
```
`x` and `y` are the current position in step counts after the move.

**Response 400** — invalid direction value

---

## POST /stop
Halt all motors immediately.

**Response 200**
```json
{"ok": true}
```

---

## POST /start_inspection
Begin the autonomous inspection route. The ESP32 handles the full movement sequence internally.

**Response 200**
```json
{"ok": true, "state": "inspection"}
```

While running, `GET /status` will return `"state": "inspection"`.
When complete (home position reached), it returns `"state": "idle"`.

---

## GET /position
Current X-Y position in step counts.

**Response 200**
```json
{"x": 120, "y": 45}
```

---

## GET /status
Full state snapshot.

**Response 200**
```json
{
  "state": "idle" | "moving" | "inspection",
  "x": 120,
  "y": 45
}
```

---

## Notes
- All endpoints return `Content-Type: application/json`
- All endpoints include `Access-Control-Allow-Origin: *` header
- Step counts are always non-negative (home = 0,0)
- The Pi is the only client — no auth needed on the local network
