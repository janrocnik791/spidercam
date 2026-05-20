# SpiderCam – Complete Setup Guide
*For someone new to Raspberry Pi development*

This guide takes you from an unboxed Pi 5 to a full working development environment where Claude Code runs on the Pi and you control everything comfortably from your laptop.

---

## What you'll end up with

```
Your Laptop
  └── VS Code (with Remote SSH)
        └── SSH tunnel
              └── Raspberry Pi 5
                    ├── Your project files
                    ├── Claude Code (runs here, touches real hardware)
                    ├── Python / Flask
                    └── USB cable → ESP32 (can flash from here)
```

---

## Part 1 — Raspberry Pi OS Setup

### Step 1: Flash the OS onto your SD card

1. On your **laptop**, download **Raspberry Pi Imager**:
   https://www.raspberrypi.com/software/

2. Insert your SD card into your laptop.

3. Open Raspberry Pi Imager and choose:
   - **Device**: Raspberry Pi 5
   - **OS**: Raspberry Pi OS (64-bit) — the full desktop version
   - **Storage**: your SD card

4. Before clicking Write, click the **gear icon ⚙** (or "Edit Settings") and fill in:
   - ✅ Set hostname: `spidercam.local`
   - ✅ Set username: `pi` and choose a password (write it down)
   - ✅ Configure Wi-Fi: enter your Wi-Fi name (SSID) and password
   - ✅ Enable SSH → Use password authentication

5. Click **Save**, then **Write**. Wait for it to finish.

6. Eject the SD card, insert it into the Pi, and power the Pi on.

> **Note:** The Pi needs a minute or two on first boot. Wait ~2 minutes before trying to connect.

---

### Step 2: Find the Pi's IP address

The Pi is now on your Wi-Fi. You need its IP address to connect.

**Option A — From your laptop (easiest):**
Open a terminal on your laptop and run:
```
ping spidercam.local
```
The IP address will appear in brackets, e.g. `192.168.1.47`.

**Option B — From your router:**
Log into your router's admin page (usually `192.168.1.1`) and look for a device named `spidercam` in the connected devices list.

**Option C — Connect Pi to a monitor temporarily:**
Plug an HDMI cable into the Pi, open a terminal on the Pi desktop, and run:
```bash
hostname -I
```

Write down the IP — you'll use it constantly.

---

### Step 3: First SSH connection from your laptop

Open a terminal on your laptop and run:
```bash
ssh pi@spidercam.local
```
Or using the IP directly:
```bash
ssh pi@192.168.1.47
```

- Type `yes` when asked about fingerprint
- Enter the password you set in step 1

You're now inside the Pi. The prompt will look like:
```
pi@spidercam:~ $
```

**You never need to plug a monitor into the Pi again.** Everything from here is done through this SSH connection.

---

### Step 4: Update the Pi

Run these commands in your SSH session (copy-paste them):
```bash
sudo apt update && sudo apt upgrade -y
```
This takes a few minutes. It's important to do once at the start.

---

## Part 2 — Install Development Tools on the Pi

Run all of these commands inside your SSH session.

### Step 5: Install Node.js (required for Claude Code)

The version of Node.js in the default Pi package manager is too old. Install the latest using this method:

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
```

Verify it worked:
```bash
node --version   # should show v20.x.x or higher
npm --version    # should show 10.x.x or higher
```

---

### Step 6: Install Claude Code

```bash
npm install -g @anthropic-ai/claude-code
```

Verify:
```bash
claude --version
```

When you first run `claude` it will ask you to log in with your Anthropic account. Run it once to complete setup:
```bash
claude
```
Follow the login prompts. After that, Claude Code is ready.

---

### Step 7: Install Python tools

The Pi comes with Python 3 already. Add pip and the virtual environment tool:
```bash
sudo apt install -y python3-pip python3-venv python3-dev
```

Also install git (for version control — very useful):
```bash
sudo apt install -y git
```

---

### Step 8: Enable I²C (for the MLX90640 thermal camera)

The MLX90640 connects via I²C. Enable it:
```bash
sudo raspi-config
```
Navigate: **Interface Options → I2C → Yes → OK → Finish**

Then reboot:
```bash
sudo reboot
```

Reconnect via SSH after ~30 seconds:
```bash
ssh pi@spidercam.local
```

Verify I²C is working:
```bash
sudo apt install -y i2c-tools
i2cdetect -y 1
```
Once the MLX90640 is wired up, you should see `33` in the grid output.

---

### Step 9: Install PlatformIO (for flashing the ESP32 from the Pi)

PlatformIO is a command-line tool that replaces Arduino IDE. It can compile and flash ESP32 code directly from the Pi over USB.

```bash
pip3 install platformio --break-system-packages
```

Add PlatformIO to your PATH so you can run `pio` from anywhere:
```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

Verify:
```bash
pio --version
```

> **When you connect the ESP32 via USB to the Pi**, it will appear as `/dev/ttyUSB0` or `/dev/ttyACM0`.
> Add yourself to the dialout group so you can flash without `sudo`:
> ```bash
> sudo usermod -aG dialout pi
> ```
> Log out and back in for this to take effect.

---

## Part 3 — Set Up VS Code on Your Laptop

You'll write and review code on your laptop using VS Code, which connects to the Pi over SSH. The files live on the Pi; your laptop is just the editor window.

### Step 10: Install VS Code

Download and install from: https://code.visualstudio.com

### Step 11: Install the Remote SSH extension

1. Open VS Code
2. Click the **Extensions** icon in the left sidebar (or press `Ctrl+Shift+X`)
3. Search for: `Remote - SSH`
4. Install the one by Microsoft

### Step 12: Connect VS Code to the Pi

1. Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on Mac) to open the command palette
2. Type: `Remote-SSH: Connect to Host`
3. Click **+ Add New SSH Host**
4. Enter: `ssh pi@spidercam.local`
5. Save to the default config file

Now connect:
1. `Ctrl+Shift+P` → `Remote-SSH: Connect to Host`
2. Select `spidercam.local`
3. Enter your password when prompted
4. VS Code will install a small server on the Pi (one time only, takes ~1 min)

Once connected, the bottom-left corner of VS Code shows `SSH: spidercam.local` in green. You're now editing files directly on the Pi.

### Step 13: Open the project folder in VS Code

1. In VS Code (while connected to Pi), click **File → Open Folder**
2. Navigate to where you'll put the project, e.g. `/home/pi/spidercam`
3. Click OK

You'll see the full file tree of the Pi on the left.

---

## Part 4 — Get the Project onto the Pi

### Step 14: Copy the project to the Pi

The project files are on your Windows laptop at:
`C:\Users\janro\Documents\Claude\Projects\OMV PIP\spidercam`

**Easiest way — use VS Code's drag and drop:**
In VS Code (connected to Pi), you can drag files from Windows Explorer into the VS Code file tree and they'll upload to the Pi.

**Alternative — use SCP from terminal:**
```bash
# Run this on your Windows laptop (PowerShell)
scp -r "C:\Users\janro\Documents\Claude\Projects\OMV PIP\spidercam" pi@spidercam.local:/home/pi/
```

After this, the project is at `/home/pi/spidercam` on the Pi.

---

### Step 15: Set up the Python virtual environment

SSH into the Pi (or use the VS Code terminal — same thing):
```bash
cd /home/pi/spidercam/pi
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

The `(.venv)` prefix in your prompt means the virtual environment is active. Always activate it before running Python:
```bash
source /home/pi/spidercam/pi/.venv/bin/activate
```

---

## Part 5 — Working with Claude Code Day to Day

### How to start a Claude Code session

1. Open the VS Code terminal (`Ctrl+\`` or Terminal → New Terminal)
2. You're already SSH'd into the Pi — no extra steps
3. Navigate to the project:
   ```bash
   cd /home/pi/spidercam
   source pi/.venv/bin/activate
   ```
4. Start Claude Code:
   ```bash
   claude
   ```

Claude Code can now read and write your project files, run Python, call `i2cdetect`, ping the ESP32, and flash code to it — all from this one session.

---

### How to use Claude Code effectively for this project

**Give it one file at a time to implement.** For example:
> "Implement `pi/app/services/esp_client.py`. Read the file first — the instructions are in the comments. The ESP32 IP is `192.168.1.100`. Test the `ping()` function against the real ESP32 once implemented."

**Tell it to test as it goes:**
> "After implementing, run a quick test: start a Python shell and call `esp_client.ping()`. Show me the output."

**For ESP32 code:**
> "Implement `esp32/src/http_server.cpp`. After writing it, use PlatformIO to compile it: `pio run` from the `esp32/` folder. Fix any errors."

**To flash the ESP32:**
> "Flash the ESP32 using PlatformIO: `pio run --target upload` from the `esp32/` folder. The device is at `/dev/ttyUSB0`."

---

### Setting up the PlatformIO project for ESP32

The `esp32/src/` folder has `.ino` and `.cpp` files but no PlatformIO config yet. Run this once to create it:

```bash
cd /home/pi/spidercam/esp32
pio project init --board esp32dev
```

This creates `platformio.ini`. Open it and make sure it contains:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    bblanchon/ArduinoJson
    me-no-dev/AsyncTCP
monitor_speed = 115200
```

Then to compile:
```bash
pio run
```

To flash (ESP32 connected via USB):
```bash
pio run --target upload
```

To open Serial Monitor (see the IP address printed at boot):
```bash
pio device monitor
```

---

## Part 6 — Phase 1 Checklist

Work through this in order. Each item is one Claude Code task.

- [ ] Implement `esp32/src/config.h` — fill in your Wi-Fi credentials
- [ ] Implement `esp32/src/position.cpp` — basic `init/get/update`
- [ ] Implement `esp32/src/motor_control.cpp` — `initMotors()` only (no movement yet)
- [ ] Implement `esp32/src/http_server.cpp` — `/ping` endpoint only
- [ ] Implement `esp32/src/main.ino` — WiFi connect + start server
- [ ] Flash to ESP32 via `pio run --target upload`, check Serial Monitor for IP
- [ ] Set `ESP32_IP` in `pi/app/config.py`
- [ ] Implement `pi/app/services/esp_client.py` — `ping()` function only
- [ ] Test: `python3 -c "from app.services.esp_client import ping; print(ping())"`
- [ ] Expected output: `True`

Once you see `True`, the Pi and ESP32 are talking. Everything else builds on that.

---

## Quick Reference

| Task | Command |
|---|---|
| SSH into Pi | `ssh pi@spidercam.local` |
| Activate Python env | `source /home/pi/spidercam/pi/.venv/bin/activate` |
| Start Claude Code | `claude` |
| Compile ESP32 | `cd esp32 && pio run` |
| Flash ESP32 | `cd esp32 && pio run --target upload` |
| ESP32 Serial Monitor | `cd esp32 && pio device monitor` |
| Check I²C devices | `i2cdetect -y 1` |
| Find ESP32 USB port | `ls /dev/tty*` (look for ttyUSB0 or ttyACM0) |
| Run Flask app | `python -m app.main` from `pi/` folder |
| Find Pi IP | `hostname -I` (run on Pi) |

---

## Troubleshooting

**"ssh: Could not resolve hostname spidercam.local"**
Use the IP address directly instead: `ssh pi@192.168.1.47`

**Claude Code says "command not found"**
Run `source ~/.bashrc` then try again. If still missing: `npm install -g @anthropic-ai/claude-code`

**`pio` command not found**
Run `source ~/.bashrc` to reload PATH, or use the full path: `~/.local/bin/pio`

**ESP32 not detected at `/dev/ttyUSB0`**
Try `ls /dev/tty*` before and after plugging in the USB — the new entry is your ESP32.
Also ensure: `sudo usermod -aG dialout pi` then log out and back in.

**`i2cdetect -y 1` shows nothing**
Check wiring (SDA→pin 3, SCL→pin 5, 3.3V, GND).
Confirm I²C is enabled: `sudo raspi-config` → Interface Options → I2C.

**Python import errors for camera library**
Make sure the virtual environment is active (`source .venv/bin/activate`), then re-run `pip install -r requirements.txt`.
