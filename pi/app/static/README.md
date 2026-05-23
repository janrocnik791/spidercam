# Argus — Handoff Bundle

Browser-based operator UI for the cable-driven SpiderCam thermal
inspection gantry at OMV. This bundle is the front-end as it stands,
plus the functional spec for backend wiring.

## Start here

1. **`Argus Spec.txt`** — read first. Source of truth for FUNCTIONALITY:
   what each control commands, what each readout displays, what events
   the UI expects, data update rates, interaction flows, E-STOP behavior.
2. **`Argus.html`** — entry point. Open in a browser to see the UI.
   Source of truth for LAYOUT, VISUALS, and component behavior.

The spec and the HTML together are the contract. Don't change the
visual layout to fit the backend — change the backend (or the demo
data shim in `v6-data.jsx`) to feed the UI what it expects.

## Files

| File | Role |
|---|---|
| `Argus.html` | Entry point. Loads scripts, mounts `<V6Argus />`, scales 1440×900 to fit. |
| `shared.jsx` | Icons, `ThermalFeed` SVG, `HistoryThumb` SVG, coverage-map primitives. |
| `v6-data.jsx` | **Demo dataset.** Exports `DETECTIONS`, `PRIORITY`, `STATUS`. Replace with live data when wiring the backend — the schema is the contract (see §5 of the spec). |
| `v6-argus.jsx` | Main app: header, hero feed, right rail (coverage + inspection + manual tabs), history strip, alarm modal, root `V6Argus` component. |
| `v6-history-detail.jsx` | Full-overlay history archive page (filter bar, list, detail view). |
| `Argus Spec.txt` | Functional spec — read this. |
| `Argus Mobile.html` | Companion phone view (not loaded by `Argus.html`). |
| `Argus Logo.html` | Logo brand sheet with iteration variants. |

## What to replace when wiring real hardware

Search for these in the code — they are the demo seams the spec calls
out in §10:

- `v6-argus.jsx` — root `V6Argus` fires a demo alarm 2.5 s after mount
  using `DETECTIONS[0]`. Replace with a real detection-event subscription.
- `v6-data.jsx` — `DETECTIONS` array is hand-crafted. Replace with a
  live list maintained from backend events.
- `v6-argus.jsx` — autonomous progress (`0.42` = 42 %) is hard-coded.
  Wire to real scan progress.
- `v6-argus.jsx` — positions (`X 0842`, `Y 0314`, `Z 1280`, etc.) are
  demo strings. Wire to live motor encoder positions.
- `v6-argus.jsx` — status icon states in `V6Header` are hard-coded
  (3 OK + 1 WARN). Wire to real subsystem health.
- `v6-argus.jsx` — mission line clock (`14:22:08 UTC`) is hard-coded.
  Tick at 1 Hz.
- `v6-argus.jsx` — REC indicator timer + fps are hard-coded.

## Transport

Spec is transport-agnostic. WebSocket, MQTT, SSE, HTTP polling, WebRTC
— pick what fits. See spec §8 for required data rates.

## Hardware

- ESP32 — motor controller (4 cables). Receives motion commands,
  reports motor state, position, cable tension.
- Raspberry Pi — hosts thermal + optical cameras, runs the difference
  detection pipeline, hosts the web server.
- This UI — browser client over WiFi.

Detection classification happens upstream of the UI; by the time
detection events reach the UI they already have a priority and saved
frames.
