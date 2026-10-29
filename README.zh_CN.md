<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# FoloToy AI Passport 开发与 Agent 引导指南

本项目基于官方开源硬件平台 [FoloToy AI Passport](https://ai-passport.folotoy.cn/)，旨在提供标准固件基线、板级支持包（BSP）以及丰富的二次开发应用。

本仓库已配置好专供 AI 编程智能体（Claude Code、Codex、Cursor、Windsurf、Orca 等）协同开发的完整工作流与核心 AI 技能。

---

## ⚡️ 快速起步（给开发者与 AI Agent）

要在新电脑上快速接入并继续进行 AI Passport 开发，请遵循以下流程：

### 1. 拉取仓库代码
```bash
git clone https://github.com/RickRenNeo/AI-Passport.git
cd AI-Passport
```

### 2. 一键环境与技能自检
在仓库根目录执行自检脚本，自动检查环境依赖并链接必备的 5 个 AI 技能：
```bash
./tools/quick_start.sh
```

---

## 🛠 开发环境搭建

本项目严格依赖 **ESP-IDF v5.5.3** 与 **ESP32-C3** 工具链。

### 1. 系统构建工具
- **macOS**：
  ```bash
  brew install cmake ninja ccache dfu-util actionlint
  ```
- **Ubuntu / Debian**：
  ```bash
  sudo apt-get update && sudo apt-get install -y cmake ninja-build ccache dfu-util python3 python3-pip python3-venv libusb-1.0-0
  ```

### 2. 安装与激活 ESP-IDF v5.5.3
建议将 ESP-IDF 安装在仓库外部路径（例如 `~/esp/esp-idf-v5.5.3`）：

```bash
mkdir -p ~/esp
# 使用乐鑫官方镜像（国内极速）
git clone --branch v5.5.3 --recursive https://git.espressif.com.cn/espressif/esp-idf.git ~/esp/esp-idf-v5.5.3
# 安装 ESP32-C3 工具链与依赖
IDF_GITHUB_ASSETS=dl.espressif.cn/github_assets ~/esp/esp-idf-v5.5.3/install.sh esp32c3
```

在当前终端中激活 ESP-IDF：
```bash
TMPDIR=/tmp source ~/esp/esp-idf-v5.5.3/export.sh
idf.py --version  # 必须输出 ESP-IDF v5.5.3
```

---

## 🔌 连接设备、验证与烧录

### 1. 设备连接
通过具备数据传输能力的 USB-C 线缆将 FoloToy AI Passport 连接至电脑。
- macOS 识别端口通常为：`/dev/cu.usbmodem*`
- Linux 识别端口通常为：`/dev/ttyACM*` 或 `/dev/ttyUSB*`

### 2. 静态检查与宿主机单测
无需硬件连接，随时在宿主机验证逻辑：
```bash
./tools/validate.sh --static
```

### 3. 完整固件构建与门禁验证
编译生成用于 `0x0` 烧录的完整合并镜像 `build/FoloToy-AI-Passport-full.bin`：
```bash
./tools/validate.sh --firmware
```

### 4. 固件烧录与串口监视
在已激活 ESP-IDF 的终端中执行：
```bash
idf.py -p <串口设备路径> flash monitor
# 例如 macOS：
idf.py -p /dev/cu.usbmodem101 flash monitor
```

---

## 🤖 AI Agent 开发规范守则

在本仓库与 AI 助手协作时，请确保 AI 遵守以下核心规则（详见 [AGENTS.zh_CN.md](AGENTS.zh_CN.md)）：
1. **必需技能**：必须先运行 `python3 tools/install_passport_skills.py --install` 确保 5 项核心技能就绪。
2. **分支管理**：`main` 分支仅为基线，**严禁在 `main` 直接开发业务**；新玩法必须检出独立分支 `feature/<app-name>`。
3. **UI 重新设计**：二次开发应用必须独立设计 UI，严禁套用或沿用 demo 菜单。
4. **硬件契约**：所有按键、屏幕、I2C 引脚及电压阈值唯一定义于 [components/bsp/include/bsp_pins.h](components/bsp/include/bsp_pins.h)，禁止应用层硬编码。
5. **并发与中断安全**：
   - LVGL 非线程安全，跨任务调用必须加锁 `bsp_lvgl_lock()` / `bsp_lvgl_unlock()`；
   - 按键回调中严禁执行慢操作，音频播放与 Flash/NVS 写入必须交由 Worker 任务处理。
