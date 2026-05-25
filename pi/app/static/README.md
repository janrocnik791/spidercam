# Argus — Handoff Bundle

Browser-based operator UI for the cable-driven SpiderCam thermal
inspection gantry at OMV. This bundle is the front-end as it stands,
plus the functional spec for backend wiring.

> **Status (May 2026):** the backend is now wired. The UI runs on **live
> SocketIO data** from the Pi — detections, scan progress, head position,
> system status, and both camera feeds — not demo data. The demo seams listed
> under *Demo seams — now wired* below have been replaced; that section is kept
> as a record. See the repo-root `README.md` for the overall project status.

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
| `v6-data.jsx` | Exports `PRIORITY`, `STATUS` (static colour/label maps) and `DETECTIONS` — **now empty**: live detection records arrive over the SocketIO `new_detection` event. The schema is still the contract (see §5 of the spec). |
| `v6-argus.jsx` | Main app: header, hero feed, right rail (coverage + inspection + manual tabs), history strip, alarm modal, root `V6Argus` component. |
| `v6-history-detail.jsx` | Full-overlay history archive page (filter bar, list, detail view). |
| `Argus Spec.txt` | Functional spec — read this. |
| `Argus Mobile.html` | Companion phone view (not loaded by `Argus.html`). |
| `Argus Logo.html` | Logo brand sheet with iteration variants. |

## Demo seams — now wired

These were the demo seams the spec calls out in §10. All are now fed by live
backend data over SocketIO:

- **Detections** — the demo alarm timer is gone; alarms now raise from the
  `new_detection` event, and `DETECTIONS` in `v6-data.jsx` is empty.
- **Scan progress** — wired to `scan_progress` (the `0.42` literal that remains
  is only a default argument on `V6SweptCoverage`, always overridden by live data).
- **Head position** — wired to `position_update`.
- **Subsystem health** icons in `V6Header` — wired to `status_update`.
- **Mission clock** ticks at 1 Hz in the browser; the REC timer and fps update
  from the live stream.

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
