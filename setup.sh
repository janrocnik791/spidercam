#!/bin/bash
# setup.sh — Restore the Spidercam development environment on a freshly flashed Raspberry Pi 5.
# Run this once after flashing the OS. It installs Node.js, Claude Code, the Python venv,
# and the system packages needed for I2C/GPIO work on the Pi.

# Install Node.js 20 from the NodeSource APT repository.
# The default Raspberry Pi OS Node version is too old for Claude Code and modern tooling.
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs

# Configure npm to install global packages into the user's home directory.
# This avoids needing sudo for `npm install -g` and prevents EACCES permission errors.
mkdir -p ~/.npm-global
npm config set prefix '~/.npm-global'

# Make the npm-global bin directory available on PATH for future shell sessions.
# Only append the export line if it isn't already present in ~/.bashrc.
if ! grep -q 'export PATH=~/.npm-global/bin:$PATH' ~/.bashrc; then
  echo 'export PATH=~/.npm-global/bin:$PATH' >> ~/.bashrc
fi
export PATH=~/.npm-global/bin:$PATH

# Install Claude Code globally so the `claude` CLI is available system-wide for the user.
npm install -g @anthropic-ai/claude-code

# Create a Python virtual environment for the Pi-side code and install its dependencies.
# Flask serves the web UI; the adafruit packages drive the MLX90640 thermal sensor over I2C.
cd /home/pi/projects/spidercam/pi
python3 -m venv .venv
source .venv/bin/activate
pip install flask adafruit-circuitpython-mlx90640 adafruit-blinka --break-system-packages

# Install OS-level packages required for hardware access.
# python3-lgpio backs Blinka's GPIO support on the Pi 5; i2c-tools provides i2cdetect for
# verifying that the thermal sensor is wired up correctly; git is needed for source control.
sudo apt-get install -y python3-lgpio i2c-tools git

echo "Setup complete. Run 'source ~/.bashrc' to reload your PATH."
