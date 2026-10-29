#!/usr/bin/env bash
# Quickstart & environment diagnostic script for AI Agents and Developers
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

echo "=================================================="
echo "  FoloToy AI Passport - Quickstart & Diagnostic"
echo "=================================================="

# 1. Check Python
if ! command -v python3 >/dev/null 2>&1; then
    echo "❌ python3 not found. Please install Python 3.10+." >&2
    exit 1
fi
echo "✅ Python: $(python3 --version)"

# 2. Install repository-local AI skills
echo ""
echo "--- [1/4] Checking & Installing AI Skills ---"
python3 tools/install_passport_skills.py --install

# 3. Check system build tools
echo ""
echo "--- [2/4] Checking System Build Tools ---"
missing_tools=()
for tool in cmake ninja ccache dfu-util; do
    if command -v "${tool}" >/dev/null 2>&1; then
        echo "✅ ${tool}: $(${tool} --version 2>&1 | head -n 1)"
    else
        echo "⚠️  ${tool}: missing"
        missing_tools+=("${tool}")
    fi
done

if [[ ${#missing_tools[@]} -gt 0 ]]; then
    echo "👉 Install missing tools via:"
    if [[ "$(uname -s)" == "Darwin" ]]; then
        echo "   brew install ${missing_tools[*]}"
    elif [[ -f /etc/debian_version ]]; then
        echo "   sudo apt-get update && sudo apt-get install -y ${missing_tools[*]}"
    fi
fi

# 4. Check ESP-IDF environment
echo ""
echo "--- [3/4] Checking ESP-IDF v5.5.3 Environment ---"
idf_ready=false
if command -v idf.py >/dev/null 2>&1; then
    idf_ver="$(idf.py --version 2>&1 || true)"
    echo "Current idf.py: ${idf_ver}"
    if [[ "${idf_ver}" == *"5.5.3"* ]]; then
        echo "✅ Active ESP-IDF matches required v5.5.3"
        idf_ready=true
    else
        echo "⚠️  Active ESP-IDF is not v5.5.3. Required version: ESP-IDF v5.5.3."
    fi
fi

if [[ "${idf_ready}" != "true" ]]; then
    default_idf_path="${HOME}/esp/esp-idf-v5.5.3"
    if [[ -f "${default_idf_path}/export.sh" ]]; then
        echo "💡 Found ESP-IDF at ${default_idf_path}. Activate it using:"
        echo "   TMPDIR=/tmp source ${default_idf_path}/export.sh"
    else
        echo "👉 ESP-IDF v5.5.3 not detected. To install via Espressif mirror (fast in CN):"
        echo "   mkdir -p ~/esp"
        echo "   git clone --branch v5.5.3 --recursive https://git.espressif.com.cn/espressif/esp-idf.git ~/esp/esp-idf-v5.5.3"
        echo "   IDF_GITHUB_ASSETS=dl.espressif.cn/github_assets ~/esp/esp-idf-v5.5.3/install.sh esp32c3"
    fi
fi

# 5. Discover USB devices
echo ""
echo "--- [4/4] Discovering Connected USB Devices ---"
usb_found=false
if [[ "$(uname -s)" == "Darwin" ]]; then
    ports=$(ls /dev/cu.usb* /dev/cu.wch* 2>/dev/null || true)
elif [[ "$(uname -s)" == "Linux" ]]; then
    ports=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)
else
    ports=""
fi

if [[ -n "${ports}" ]]; then
    echo "Found serial ports:"
    for p in ${ports}; do
        echo "  🔌 ${p}"
    done
    usb_found=true
else
    echo "ℹ️  No AI Passport serial port detected. Connect device with a data-capable USB cable."
fi

# 6. Repository self-check
echo ""
echo "--- Running Static Repository Check ---"
python3 tools/check_repo.py

echo ""
echo "=================================================="
echo "  Setup Diagnostic Complete!"
echo "  - To run static and host tests: ./tools/validate.sh --static"
echo "  - To build merged firmware:     ./tools/validate.sh --firmware"
echo "  - To flash device:              idf.py -p <PORT> flash monitor"
echo "=================================================="
