#!/usr/bin/env bash
# Wait for the AI-Passport USB serial port, then flash the app partition and start capture.
set -u
APP="${1:?image path required}"
PORT=""

echo "waiting for /dev/cu.usbmodem* ..."
for _ in $(seq 1 1800); do
    for candidate in /dev/cu.usbmodem*; do
        [[ -e "$candidate" ]] || continue
        PORT="$candidate"
        break
    done
    [[ -n "$PORT" ]] && break
    sleep 2
done

if [[ -z "$PORT" ]]; then
    echo "no port appeared after 60 minutes"
    exit 1
fi

echo "device appeared at $PORT, flashing $APP"
sleep 2
./venv_gateway/bin/esptool.py --chip esp32c3 --port "$PORT" --baud 921600 \
    write_flash -z 0x20000 "$APP" 2>&1 | tail -5

echo "restarting serial capture"
pkill -f capture_serial.py 2>/dev/null
pkill -f watch_tianji_test.py 2>/dev/null
sleep 1
./venv_gateway/bin/python3 tools/capture_serial.py
