<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Release v20261105 - FoloToy AI Passport (feature/xiaozhi-tianji)

- **Firmware Version**: v20261105
- **Branch / Commit**: `feature/xiaozhi-tianji` (`7acebf6`)
- **Target Hardware**: ESP32-C3 / 8MB Flash / 240x320 LCD
- **Firmware Binary**:
  - `FoloToy-AI-Passport-v20261105-full.bin` (2,550,608 bytes)
  - `FoloToy-AI-Passport-full.bin` (latest symlink/mirror)
- **SHA-256**: `e3ab2bd174b33b51e8fcf9a4feb53c78e5fb530455aba3f048ba891af065ff5e`

## Flashing Guide

### Method 1: Using esptool (Recommended, flash at offset 0x0)
```bash
python -m esptool --chip esp32c3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 release/FoloToy-AI-Passport-v20261105-full.bin
```

Specify a serial port (e.g. on macOS):
```bash
python -m esptool -p /dev/cu.usbmodem101 --chip esp32c3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 release/FoloToy-AI-Passport-v20261105-full.bin
```

### Method 2: Web Flasher
Open [FoloToy Web Flasher](https://ai-passport.folotoy.cn/) or ESP Web Tools in Chrome/Edge, select `release/FoloToy-AI-Passport-v20261105-full.bin`, and flash to offset `0x0`.
