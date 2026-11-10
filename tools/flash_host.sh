#!/usr/bin/env bash
# Install the HID-host firmware through the available USB console/ROM port.
# Capture its UART output before rebooting; game assets are preserved.
set -euo pipefail
flash_port=${1:-${ESPPORT:-/dev/ttyACM0}}
monitor_port=${2:-/dev/ttyS1}
log=${3:-build/host-startup.log}
idf.py -B build -DDOOM_USB_HOST=ON build >build/host-build-latest.log 2>&1
python tools/flash_host.py --flash-port "$flash_port" --monitor-port "$monitor_port" --log "$log"
