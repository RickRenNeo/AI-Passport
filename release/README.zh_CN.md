<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 固件发布 v20261105 - FoloToy AI Passport (feature/xiaozhi-tianji)

- **固件版本**：v20261105
- **分支 / Commit**：`feature/xiaozhi-tianji` (`7acebf6`)
- **目标硬件**：ESP32-C3 / 8MB Flash / 240x320 LCD
- **固件文件**：
  - `FoloToy-AI-Passport-v20261105-full.bin` (2,550,608 字节)
  - `FoloToy-AI-Passport-full.bin` (镜像文件)
- **SHA-256**：`e3ab2bd174b33b51e8fcf9a4feb53c78e5fb530455aba3f048ba891af065ff5e`

## 烧录指南

### 方式一：使用 esptool 完整烧录（推荐，写入 0x0 偏移量）
```bash
python -m esptool --chip esp32c3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 release/FoloToy-AI-Passport-v20261105-full.bin
```

若指定串口（例如 macOS）：
```bash
python -m esptool -p /dev/cu.usbmodem101 --chip esp32c3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 release/FoloToy-AI-Passport-v20261105-full.bin
```

### 方式二：网页 Web 烧录工具
使用 Chrome 或 Edge 浏览器访问 [FoloToy 官网烧录工具](https://ai-passport.folotoy.cn/) 或 ESP Web Tools，选择 `release/FoloToy-AI-Passport-v20261105-full.bin` 并在地址 `0x0` 处烧录。
