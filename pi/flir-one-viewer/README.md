# FLIR One Pro Viewer — Raspberry Pi

End-to-end stack for streaming the FLIR One Pro (Android, USB-C) thermal
camera on a Raspberry Pi: a libusb-based C bridge demuxes the camera's
proprietary dual-stream protocol into standard `v4l2loopback` devices, and a
Python + Flask web viewer reads both streams with OpenCV, re-encodes them
as MJPEG, and serves them at `http://<pi-ip>:5000` so any browser on the
LAN can view live thermal + visual side-by-side (or blended) with no
plugins.

```
FLIR One Pro (USB 09cb:1996)
       │
       ▼  libusb bulk transfers
[ driver/flirone-v4l2 ]   ── writes ──▶  /dev/video1  (Y8 thermal,    160×120)
                                          /dev/video2  (MJPEG visible, 1440×1080)
                                          /dev/video3  (RGB24 colorized thermal)
       │
       ▼  V4L2
[ viewer/viewer.py ]      ── OpenCV ──▶  Flask MJPEG endpoints @ :5000
       │
       ▼  HTTP (multipart/x-mixed-replace)
[ any browser on LAN ]    ── http://<pi-ip>:5000 ──▶  side-by-side | overlay
```

The C driver is the proven `fnoop/flirone-v4l2` source (GPLv2, originally by
tomas123 on EEVblog) — see the in-file copyright header in
`driver/flirone-v4l2.c`. The Pro is the driver's default target; there is no
`--pro` flag, only a palette argument.

## Hardware

| | |
|---|---|
| Camera         | FLIR One Pro (Android, USB-C) — VID:PID `09cb:1996` |
| Pi             | Raspberry Pi 4 or 5 |
| Cable          | USB-C → USB-A; plug into a Pi USB 3.0 port |
| Power          | Pi must use its own 5 V / 3 A+ supply |

## One-time setup

```bash
cd flir-one-viewer
bash setup/install.sh
# log out and back in (or reboot) so 'video' group + udev rule apply
```

The installer:

1. installs `libusb-1.0-0-dev`, `libjpeg-dev`, `build-essential`,
   `v4l2loopback-dkms`, `python3-opencv`, `python3-numpy`, `python3-flask`
2. builds `driver/flirone-v4l2`
3. installs `setup/77-flirone-lusb.rules` so the camera is accessible to the
   `video` group (no `sudo` needed for streaming)
4. adds you to the `video` group
5. configures `v4l2loopback` to auto-load on boot with 3 devices on
   `video_nr=1,2,3`

## Every run

```bash
bash setup/start.sh
# optional palette override:
FLIR_PALETTE=$PWD/driver/palettes/Rainbow.raw bash setup/start.sh
# optional port override (default 5000):
FLIR_PORT=8080 bash setup/start.sh
```

`start.sh` ensures `v4l2loopback` is loaded, launches the C driver in the
background, waits 3 s for the camera to enumerate, then runs the Flask web
viewer. It prints the LAN URL (`http://<pi-ip>:5000`) — open that on any
device on the same network. `Ctrl-C` in the terminal stops the viewer and
the `trap` tears the driver down too.

## Web interface

Open `http://<pi-ip>:5000` in a browser (the exact URL is printed by
`start.sh`). The page shows thermal on the left, visual on the right, with
a button that toggles between **side-by-side** and **overlay** (thermal
blended over visual at α=0.45). It's a single static page that pulls each
feed as a long-lived `multipart/x-mixed-replace` MJPEG stream into an
`<img>` tag, so it works on any modern desktop or mobile browser without
WebRTC, plugins, or JavaScript media APIs.

Endpoints:

| route | type | content |
|---|---|---|
| `GET /`                | `text/html`                  | the single-page viewer UI |
| `GET /stream/thermal`  | `multipart/x-mixed-replace`  | Y8 thermal → contrast-stretched → INFERNO colormap, upscaled to 640×480 |
| `GET /stream/visual`   | `multipart/x-mixed-replace`  | visible MJPEG re-encoded at 640×480 |
| `GET /stream/overlay`  | `multipart/x-mixed-replace`  | server-side blend of the above two (α=0.45) |

Each MJPEG endpoint can also be opened directly in a browser, embedded in
another app (Home Assistant `mjpeg` camera entity, OBS, `ffmpeg`, `vlc`,
etc.), or recorded with `curl --output thermal.mjpeg
http://<pi-ip>:5000/stream/thermal`.

The thermal stream is the Y8 feed from `/dev/video1`, per-frame contrast
stretched then INFERNO-colormapped in Python on every frame, so contrast
adapts live to whatever's in the scene. To change the default colormap,
edit `cv2.COLORMAP_INFERNO` in `thermal_to_color()` at
`viewer/viewer.py`.

### Viewer CLI flags

```bash
python3 viewer/viewer.py --help
  --thermal N   thermal /dev/video index (default 1)
  --visual  M   visible /dev/video index (default 2)
  --host    H   bind address       (default 0.0.0.0 = all interfaces)
  --port    P   HTTP port          (default 5000)
```

## Testing sequence

```bash
lsusb | grep -i flir
# Bus 00X Device 00X: ID 09cb:1996 FLIR Systems FLIR ONE Camera

ls /dev/video*
# /dev/video1  /dev/video2  /dev/video3

# Drive only the C side, then in another shell probe the thermal device:
./driver/flirone-v4l2 driver/palettes/Iron2.raw &
python3 -c "import cv2; c=cv2.VideoCapture(1); ok,f=c.read(); print(ok, f.shape if ok else None)"
```

## Troubleshooting

**`Permission denied` on `/dev/bus/usb/...`** — udev rule hasn't applied yet
or you haven't re-logged-in since being added to `video`. Quick check:
`groups | grep video`. Last-resort smoke test: `sudo ./driver/flirone-v4l2
driver/palettes/Iron2.raw`.

**No `/dev/video1` etc.** — `v4l2loopback` isn't loaded. Run `lsmod | grep
v4l2loopback`; if empty, `start.sh` will modprobe it. If `modprobe` fails
right after a kernel upgrade, the DKMS module needs rebuilding: `sudo
dpkg-reconfigure v4l2loopback-dkms`.

**Viewer opens but no frames** — the FLIR One Pro takes 2–3 s after USB
attach to start streaming, and the visible camera tops out at ~8 fps. The
viewer retries `VideoCapture` up to 20 × 500 ms by default. If it gives up,
verify with `lsusb` that the camera is still enumerated (the Pro turns off
after long idle — unplug/replug).

**Thermal `/dev/video1` never opens, visual `/dev/video2` works** —
historically the upstream `fnoop/flirone-v4l2` source ships with the Y8
thermal output path commented out in `startv4l2()` and in the per-frame
write block; only the MJPEG visual (`fdwr1` → `/dev/video2`) and the
colorized RGB24 thermal (`fdwr2` → `/dev/video3`) are produced. Since no
producer ever opens `/dev/video1` for `V4L2_BUF_TYPE_VIDEO_OUTPUT`,
v4l2loopback has no format set and `cv2.VideoCapture(1).read()` returns
False until the viewer's 20-attempt retry loop gives up — even though the
driver itself prints normal "Ask for video stream, start EP 0x85" output.
This repo carries the fix: the `fdwr0` open + `VIDIOC_S_FMT` block in
`startv4l2()` and the `write(fdwr0, fb_proc, framesize0)` call in
`vframe()` in `driver/flirone-v4l2.c` are un-commented, so Y8 thermal is
published to `/dev/video1` as the architecture diagram intends. If you
ever pull a fresh copy of the upstream driver over this tree, re-apply
those un-comments and rebuild.

Working state after the fix — `v4l2-ctl --list-devices` should show:

```
"FLIR-Thermal" (platform:v4l2loopback-001):   /dev/video1   ← Y8 thermal,    160×120
"FLIR-Visual"  (platform:v4l2loopback-002):   /dev/video2   ← MJPEG visible, 1440×1080
"FLIR-Blend"   (platform:v4l2loopback-003):   /dev/video3   ← RGB24 colorized thermal
```

Quick probe with the driver running:

```bash
python3 -c "import cv2; c=cv2.VideoCapture(1, cv2.CAP_V4L2); ok,f=c.read(); print(ok, f.shape if ok else None)"
# expect: True (120, 160, 3)   ← v4l2loopback surfaces Y8 as 3-channel BGR with R=G=B=Y
python3 -c "import cv2; c=cv2.VideoCapture(2, cv2.CAP_V4L2); ok,f=c.read(); print(ok, f.shape if ok else None)"
# expect: True (1080, 1440, 3)   ← Pro visible sensor is 1440×1080; viewer downscales to 640×480
```

**Visual `/dev/video2` is a completely black screen, thermal works** — the
visible feed decodes to all-zero (black) frames even though the thermal
stream is fine. Root cause: a frame-size mismatch between what the driver
*declares* and what the FLIR One **Pro** actually emits. The Pro's visible
sensor produces a **1440×1080** JPEG on EP 0x85, but the upstream driver was
written for the gen-2 camera and hard-coded the `/dev/video2` v4l2loopback
output format to **640×480** (`FRAME_WIDTH1`/`FRAME_HEIGHT1`). The driver
still `write()`s the real, full-size JPEG bytes (`JpgSize` from the camera),
so the *device* is valid — but because v4l2loopback advertises the format as
640×480 MJPG, OpenCV's V4L2 capture sizes its libjpeg decode buffer for a
640×480 frame and then aborts decoding the larger 1440×1080 JPEG with
`Corrupt JPEG data: premature end of data segment`, returning a zeroed
(black) buffer. Diagnosis that pins it down:

```bash
# raw JPEG straight off the loopback decodes to a real 1440×1080 image:
python3 - <<'PY'
import os, cv2, numpy as np
fd = os.open('/dev/video2', os.O_RDONLY); d = os.read(fd, 2_000_000); os.close(fd)
s = d.find(b'\xff\xd8'); e = d.rfind(b'\xff\xd9')
img = cv2.imdecode(np.frombuffer(d[s:e+2], np.uint8), cv2.IMREAD_COLOR)
print('raw jpeg decodes to', img.shape, 'mean', round(float(img.mean()),1))  # (1080, 1440, 3)
PY
# but OpenCV's V4L2 path, sized for 640×480, returns black:
python3 -c "import cv2; c=cv2.VideoCapture(2,cv2.CAP_V4L2); ok,f=c.read(); print(f.shape, f.mean())"
```

The fix in this repo is to declare the true resolution: `FRAME_WIDTH1
1440` / `FRAME_HEIGHT1 1080` in `driver/flirone-v4l2.c`. Then v4l2loopback
advertises 1440×1080 MJPG, OpenCV negotiates the matching format, libjpeg
decodes the whole frame, and `viewer.py`'s `fit_visual()` downscales it to
the 640×480 it serves. If you pull a fresh upstream driver over this tree,
re-apply that resolution change and rebuild, or the visual feed goes black
again. (A consumer-side workaround also exists — set FOURCC `MJPG` plus a
1440×1080 frame size on the `VideoCapture` so OpenCV allocates a large
enough decode buffer — but declaring the real size in the driver is the
root-cause fix and works for any consumer, not just this viewer.)

**Both feeds show a single frozen frame instead of a live stream** — you open
the page, thermal and visual each render one image, and then nothing moves.
This means the C driver died and stopped feeding the loopback devices: with
no producer, the viewer's background `VideoCapture` threads log
`select() timeout` on `/dev/video1` and `/dev/video2`, keep the last frame
they managed to grab, and the MJPEG endpoints re-serve that stale frame
forever. Confirm with `tail /tmp/driver.log` (or wherever `start.sh`'s output
goes) — a dead driver leaves a trail like:

```
EP 0x83 LIBUSB_ERROR_NO_DEVICE -> reset USB
flirone-v4l2: flirone-v4l2.c:NNN: startv4l2: Assertion `ret_code != -1' failed.
```

Root cause is in the driver's reconnect path. The FLIR One **Pro**
periodically drops off USB (`LIBUSB_ERROR_NO_DEVICE` — the camera FFC/power
cycles), so `main()` runs `EPloop()` inside a `while(1)` to re-establish the
connection. The upstream code called `startv4l2()` *inside* `EPloop()`, i.e.
on every reconnect, which re-`open()`ed the already-open `/dev/video1,2,3`
loopback devices and re-issued `VIDIOC_S_FMT`. The second `S_FMT` fails with
`EBUSY` — the previous (leaked) producer handle and the viewer's active
capture both still hold the format — so `assert(ret_code != -1)` aborts the
whole process. A related `libusb_reset_device(devh)` on the `out:` cleanup
path also segfaulted when the camera couldn't be opened at all (`devh` NULL).

The fix in this repo (`driver/flirone-v4l2.c`):

1. `startv4l2()` is called **once** in `main()`, before the `while(1)` loop —
   the loopback outputs are opened for the whole process lifetime and the
   format is set exactly once, so reconnects never touch them. `EPloop()` no
   longer calls it.
2. The `out:` cleanup guards `if (devh) { libusb_reset_device(...); ... }` so a
   missing/booting camera triggers a graceful retry instead of a NULL-deref
   crash.
3. `main()` sleeps 1 s between `EPloop()` retries so a persistently-absent
   camera doesn't spin at 100 % CPU.

With these, the driver survives the Pro's periodic USB drops: the stream
briefly holds its last frame during the gap, then resumes live the moment the
camera re-enumerates — no manual restart, no permanent freeze. If you pull a
fresh upstream driver over this tree, re-apply these (the upstream `startv4l2()`
call sits in `EPloop()`), or the feed will freeze on the first USB reset.

Note: if the camera has been hammered with rapid restarts/resets it can wedge
into an *enumerated-but-not-streaming* state (the driver connects and polls
but every USB read returns `EAGAIN`, so no frames are written and reads
`select() timeout`). The driver can't fix this — physically unplug and replug
the camera to power-cycle its sensor, then re-run `start.sh`.

**Wrong /dev/video numbers** — pass `--thermal N --visual M` to
`viewer.py`. The C driver hardcodes 1/2/3 internally; if you have a CSI
camera occupying a low number, change `video_nr=` in
`/etc/modprobe.d/v4l2loopback.conf` *and* edit the `VIDEO_DEVICE*` defines
at the top of `driver/flirone-v4l2.c`, then rebuild.

**Temperatures are off by several °C** — expected. The Planck constants in
`driver/plank.h` are a generic reverse-engineered set; per-unit factory
calibration lives in the camera's own firmware and isn't exposed to libusb.

## Credits

- USB protocol reverse-engineering: **tomas123** & **cynfab** on the
  [EEVblog forum](https://www.eevblog.com/forum/thermal-imaging/)
- v4l2loopback packaging: [`fnoop/flirone-v4l2`](https://github.com/fnoop/flirone-v4l2)
- Gen-3 adaptation notes: [`Miso98/hw-flir-one-gen3`](https://github.com/Miso98/hw-flir-one-gen3)
- Background reading: Nicole Faerber's blog at dpin.de;
  `infincia/flir-one-documentation` wiki

GPLv2 — same as the upstream driver.
