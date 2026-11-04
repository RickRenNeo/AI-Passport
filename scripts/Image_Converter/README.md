**English** · [简体中文](/scripts/Image_Converter/README.zh_CN.md)

# LVGL image converter

This directory holds two Python scripts for processing and converting images to
LVGL formats.

## 1. LVGLImage (LVGLImage.py)

Taken from the [LVGL repository](https://github.com/lvgl/lvgl)
[LVGLImage.py](https://github.com/lvgl/lvgl/blob/master/scripts/LVGLImage.py).

## 2. LVGL image conversion tool (lvgl_tools_gui.py)

Wraps `LVGLImage.py` to batch-convert images into LVGL image format. It can be
used to replace the assistant's default emoji; a walkthrough is available
[here](https://www.bilibili.com/video/BV12FQkYeEJ3/).

### Features

- Graphical interface
- Batch conversion
- Automatic input format detection with the best matching color format
- Multiple resolutions

### Usage

Create a virtual environment:

```bash
python -m venv venv
source venv/bin/activate  # Linux/Mac
venv\Scripts\activate     # Windows
```

Install the dependencies:

```bash
pip install -r requirements.txt
```

Run the tool:

```bash
source venv/bin/activate  # Linux/Mac
venv\Scripts\activate     # Windows
python lvgl_tools_gui.py
```
