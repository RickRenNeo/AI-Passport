<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# FoloToy AI Passport Development & Agent Guide

This repository is built upon the open hardware platform [FoloToy AI Passport](https://ai-passport.folotoy.cn/en/), providing official firmware baselines, Board Support Packages (BSP), and derivative applications.

The workspace is tailored for AI coding assistants (Claude Code, Codex, Cursor, Windsurf, Orca, etc.) with pre-integrated development workflows and dedicated AI skills.

---

## ⚡️ Quickstart for Developers and AI Agents

To quickly set up and continue AI Passport development on a new machine:

### 1. Clone Repository
```bash
git clone https://github.com/RickRenNeo/AI-Passport.git
cd AI-Passport
```

### 2. Environment & Skills Self-Check
Run the diagnostics script at the root of the repository to link required skills and verify dependencies:
```bash
./tools/quick_start.sh
```

---

## 🛠 Development Environment Setup

This project strictly requires **ESP-IDF v5.5.3** targeting **ESP32-C3**.

### 1. System Build Tools
- **macOS**:
  ```bash
  brew install cmake ninja ccache dfu-util actionlint
  ```
- **Ubuntu / Debian**:
  ```bash
  sudo apt-get update && sudo apt-get install -y cmake ninja-build ccache dfu-util python3 python3-pip python3-venv libusb-1.0-0
  ```

### 2. Install & Activate ESP-IDF v5.5.3
Install ESP-IDF outside the repository tree (e.g. at `~/esp/esp-idf-v5.5.3`):

```bash
mkdir -p ~/esp
# Clone upstream or Espressif mirror
git clone --branch v5.5.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.3
# Install ESP32-C3 toolchain and Python environment
~/esp/esp-idf-v5.5.3/install.sh esp32c3
```

Activate ESP-IDF in your current terminal session:
```bash
TMPDIR=/tmp source ~/esp/esp-idf-v5.5.3/export.sh
idf.py --version  # Must output ESP-IDF v5.5.3
```

---

## 🔌 Device Connection, Validation & Flashing

### 1. Device Connection
Connect the FoloToy AI Passport to your computer using a data-capable USB-C cable.
- macOS port pattern: `/dev/cu.usbmodem*`
- Linux port pattern: `/dev/ttyACM*` or `/dev/ttyUSB*`

### 2. Static & Host Logic Tests
Run tests without requiring connected hardware:
```bash
./tools/validate.sh --static
```

### 3. Full Firmware Build & Validation
Build and verify the full merged image (`build/FoloToy-AI-Passport-full.bin`) ready for flashing at offset `0x0`:
```bash
./tools/validate.sh --firmware
```

### 4. Flashing & Serial Monitor
Flash firmware and observe serial output:
```bash
idf.py -p <SERIAL_PORT> flash monitor
# Example on macOS:
idf.py -p /dev/cu.usbmodem101 flash monitor
```

---

## 🤖 AI Agent Engineering Baselines

When developing with AI assistants, strictly observe the following rules (detailed in [AGENTS.md](AGENTS.md)):
1. **Required Skills**: Run `python3 tools/install_passport_skills.py --install` to verify all 5 core skills before development.
2. **Branch Management**: `main` is a minimal baseline; never develop features directly on `main`. Create feature branches (`feature/<app-name>`).
3. **UI Redesign**: Derivative applications must design their own UI. Do not reuse demo menus or shell screens.
4. **Hardware Contract**: All pin maps, addresses, and ADC thresholds are defined only in [components/bsp/include/bsp_pins.h](components/bsp/include/bsp_pins.h).
5. **Concurrency & Thread Safety**:
   - LVGL is not thread-safe. Guard all LVGL calls from outside LVGL tasks with `bsp_lvgl_lock()` / `bsp_lvgl_unlock()`.
   - Button callbacks must never block. Delegate slow tasks (audio, Flash/NVS) to FreeRTOS worker tasks.
