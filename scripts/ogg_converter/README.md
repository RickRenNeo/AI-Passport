**English** · [简体中文](/scripts/ogg_converter/README.zh_CN.md)

# ogg_converter: batch OGG converter for the assistant sound effects

This script batch-converts input audio into the OGG format used by the
assistant.

It is built on the Python library `ffmpeg-python`, so an `ffmpeg` environment is
**required**. Download a build for your system
[here](https://ffmpeg.org/download.html) and either add it to `PATH` or place it
next to this script.

Features include OGG <-> audio conversion and loudness normalisation.

## Create and activate a virtual environment

```bash
# create the virtual environment
python -m venv venv
# activate it
source venv/bin/activate  # Mac/Linux
venv\Scripts\activate     # Windows
```

## Install ffmpeg

Download ffmpeg [here](https://ffmpeg.org/download.html), pick the build for your
system, and either place the `ffmpeg` executable in this directory or add its
directory to `PATH`.

## Install the dependencies

Run inside the virtual environment:

```bash
pip install ffmpeg-python
```

## Run the script

```bash
python ogg_covertor.py
```
