# Spidercam — Demo interfaces

Standalone demo UIs served by the **waypoint server** (`waypoint_server.py`, port
8765) — separate from the Argus thermal software in `pi/`.

```
demo/
├── movement/   — inspection movement interface  (served at /demo/movement)
│   └── movement.html
└── thermal/    — thermal demo assets (reserved; drop images here to use them)
    ├── baseline/   thermal_baseline.jpg   (served at /demo/thermal/baseline)
    ├── current/    thermal_current.jpg    (served at /demo/thermal/current)
    └── sequence/   frame_001.jpg, …       (listed at /demo/thermal/sequence)
```

Open the movement demo at: http://192.168.253.249:8765/demo/movement

It talks directly to the hardware (same pattern as `/pathmarker`): the ESP32 at
`192.168.253.85` for motion/route, and this waypoint server for corner status.
