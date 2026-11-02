#!/usr/bin/env bash
# Helper to reliably activate ESP-IDF 5.5.3 even when TMPDIR contains spaces.
set -e

if [ -z "${IDF_PATH:-}" ]; then
    if [ -d "$HOME/esp/esp-idf-v5.5.3" ]; then
        export IDF_PATH="$HOME/esp/esp-idf-v5.5.3"
    elif [ -d "$HOME/esp/esp-idf" ]; then
        export IDF_PATH="$HOME/esp/esp-idf"
    elif [ -d "/Users/mac/esp/esp-idf-v5.5.3" ]; then
        export IDF_PATH="/Users/mac/esp/esp-idf-v5.5.3"
    fi
fi

if [ -n "${IDF_PATH:-}" ] && [ -f "${IDF_PATH}/tools/activate.py" ]; then
    ACTIVATE_OUT=$(python3 "${IDF_PATH}/tools/activate.py" --export --shell bash 2>/dev/null || true)
    SCRIPT_PATH=$(echo "${ACTIVATE_OUT}" | grep '^\\. ' | head -n 1 | cut -c 3-)
    if [ -n "${SCRIPT_PATH}" ] && [ -f "${SCRIPT_PATH}" ]; then
        . "${SCRIPT_PATH}"
    elif [ -f "${IDF_PATH}/export.sh" ]; then
        . "${IDF_PATH}/export.sh"
    fi
fi
