<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### eCard 界面字库

[`fonts/`](fonts/) 保存 eCard 界面使用的五套 LVGL 点阵字库：
`ecard_font_9.c`、`ecard_font_12.c`、`ecard_font_16.c`、`ecard_font_22.c`
与 `ecard_font_48.c`。它们由 `main/CMakeLists.txt` 编译进 `main`，
并在 `main/ecard_ui.c` 中由每个文本控件显式指定。

| 项目 | 值 |
| --- | --- |
| 源字族 | Noto Sans CJK SC Regular，标签 `Sans2.004` |
| 源文件 URL | `https://github.com/notofonts/noto-cjk/raw/Sans2.004/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf` |
| 源文件 SHA-256 | `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b` |
| 许可 | SIL Open Font License 1.1，[`fonts/LICENSE-NotoSansCJK.txt`](fonts/LICENSE-NotoSansCJK.txt) |
| 转换器 | `lv_font_conv` 1.5.3，`--bpp 2 --format lvgl`（4 级灰阶 + `CONFIG_LV_USE_FONT_COMPRESSED=y` RLE 无损压缩） |
| 覆盖范围 | 可打印 ASCII + eCard 源码及 `tools/ecard_curriculum_data.py` 中的全部非 ASCII 字符 + 界面符号；12/16/22 px 额外包含 GB2312 一级字库，确保用户配置的学校、班级、姓名可正常显示；9 px 与 48 px 仅含 ASCII |
| 编译后大小 | 约 0.65 MiB 压缩字库数据，位于 Flash（零系统堆开销） |

在仓库根目录重新生成已提交的字库源码：

```bash
npm install --prefix /tmp/lvfont lv_font_conv@1.5.3
python3 tools/generate_ecard_fonts.py \
  --converter /tmp/lvfont/node_modules/.bin/lv_font_conv
```

脚本会把固定版本的 OTF 下载到 `fonts/.cache/`，校验 SHA-256，并在源字库
缺少任一所需字形时失败。缓存的 OTF 不提交。修改界面文案后必须重新运行
脚本与 `python3 tests/test_ecard_fonts.py`；`./tools/validate.sh --static`
也会执行覆盖检查。生成清单之外的字符不受支持，将显示为 LVGL 缺字占位符。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |
| [`images/ecard_portrait_source.jpg`](images/ecard_portrait_source.jpg) | 665 × 886，JPEG | 仅供 eCard 演示使用的合成半身照，不对应任何真实学生；量产前必须替换为学校授权的证件照。于 2026-11-06 为本仓库生成。 |
| [`images/ecard_portrait.c`](images/ecard_portrait.c) | 130 × 184 与 54 × 78，RGB565 C 数组 | 由 `tools/generate_ecard_photo.py` 生成，圆角已烘焙为白色，编译进 `main` 并通过 `main/ecard_photo.h` 引用。请勿手工编辑。 |

替换源图后，在仓库根目录重新生成证件照描述符：

```bash
python3 tools/generate_ecard_photo.py
```

脚本会把预览裁切图写入已被忽略的 `images/.ecard_portrait_preview/` 目录。

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。
