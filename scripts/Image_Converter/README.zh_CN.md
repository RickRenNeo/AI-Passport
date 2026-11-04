[English](/scripts/Image_Converter/README.md) · **简体中文**

# LVGL 图片转换工具

这个目录包含两个用于处理和转换图片为 LVGL 格式的 Python 脚本。

## 1. LVGLImage (LVGLImage.py)

引用自 LVGL [官方 repo](https://github.com/lvgl/lvgl) 的转换脚本
[LVGLImage.py](https://github.com/lvgl/lvgl/blob/master/scripts/LVGLImage.py)。

## 2. LVGL 图片转换工具 (lvgl_tools_gui.py)

调用 `LVGLImage.py`，将图片批量转换为 LVGL 图片格式；可用于修改小智的默认表情，
具体修改教程[在这里](https://www.bilibili.com/video/BV12FQkYeEJ3/)。

### 特性

- 图形化操作，界面更友好
- 支持批量转换图片
- 自动识别图片格式并选择最佳的颜色格式转换
- 多分辨率支持

### 使用方法

创建虚拟环境：

```bash
python -m venv venv
source venv/bin/activate  # Linux/Mac
venv\Scripts\activate     # Windows
```

安装依赖：

```bash
pip install -r requirements.txt
```

运行转换工具：

```bash
source venv/bin/activate  # Linux/Mac
venv\Scripts\activate     # Windows
python lvgl_tools_gui.py
```
