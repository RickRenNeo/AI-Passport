#!/usr/bin/env bash
# Helper to reliably activate ESP-IDF 5.5.3 even when TMPDIR contains spaces.
set -e
export IDF_PATH="/Users/mac/esp/esp-idf-v5.5.3"
ACTIVATE_OUT=$(python3 "${IDF_PATH}/tools/activate.py" --export --shell bash 2>/dev/null || /Users/mac/.espressif/python_env/idf5.5_py3.12_env/bin/python "${IDF_PATH}/tools/activate.py" --export --shell bash)
SCRIPT_PATH=$(echo "${ACTIVATE_OUT}" | grep '^\. ' | head -n 1 | cut -c 3-)
if [ -n "${SCRIPT_PATH}" ] && [ -f "${SCRIPT_PATH}" ]; then
    . "${SCRIPT_PATH}"
fi
