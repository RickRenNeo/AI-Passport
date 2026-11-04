**English** · [简体中文](/scripts/spiffs_assets/README.zh_CN.md)

# SPIFFS assets builder

This script builds the SPIFFS partition image of an ESP32 project, packing the
resource files into formats the device can use.

## Features

- Processes the wake-word network model (WakeNet)
- Integrates text font files
- Processes the emoji image collection
- Generates the resource index
- Packs the final `assets.bin`

## Requirements

- Python 3.6+
- The resource files themselves

## Usage

### Syntax

```bash
./build.py --wakenet_model <wakenet_model_dir> \
    --text_font <text_font_file> \
    --emoji_collection <emoji_collection_dir>
```

### Options

| Option | Type | Required | Description |
| --- | --- | --- | --- |
| `--wakenet_model` | directory | no | Wake-word network model directory |
| `--text_font` | file | no | Text font file |
| `--emoji_collection` | directory | no | Emoji image collection directory |

### Examples

```bash
# all options
./build.py \
    --wakenet_model ../../managed_components/espressif__esp-sr/model/wakenet_model/wn9_nihaoxiaozhi_tts \
    --text_font ../../components/noto-fonts/cbin/font_noto_sans_common_20_4.bin \
    --emoji_collection ../../components/noto-fonts/png/noto-color-emoji_64/

# font only
./build.py --text_font ../../components/noto-fonts/cbin/font_noto_sans_common_20_4.bin

# emoji only
./build.py --emoji_collection ../../components/noto-fonts/png/noto-color-emoji_64/
```

## Workflow

1. **Lay out the build directories**
   - `build/` - main build directory
   - `build/assets/` - resource files
   - `build/output/` - output files
2. **Process the wake-word network model**
   - Copy the model files into the build directory
   - Generate `srmodels.bin` with `pack_model.py`
   - Copy the generated model files into the resource directory
3. **Process the text font**
   - Copy the font file into the resource directory
   - `.bin` font files are supported
4. **Process the emoji collection**
   - Scan the given directory for images
   - `.png` and `.gif` are supported
   - Generate the emoji index
5. **Generate the configuration files**
   - `index.json` - resource index
   - `config.json` - build configuration
6. **Pack the final resources**
   - Generate `assets.bin` with `spiffs_assets_gen.py`
   - Copy it to the build root

## Output

After a successful build, `build/` contains:

- `assets/` - all resource files
- `assets.bin` - the final SPIFFS image
- `config.json` - build configuration
- `output/` - intermediate output

## Supported formats

- **Model files**: `.bin` (processed through `pack_model.py`)
- **Font files**: `.bin`
- **Images**: `.png`, `.gif`
- **Configuration**: `.json`

## Error handling

The script reports failing preconditions instead of guessing:

- Checks whether the source file/directory exists
- Verifies subprocess results
- Prints detailed errors and warnings

## Notes

1. Keep all dependent Python scripts in the same directory as this one.
2. Resource paths may be absolute or relative to the script directory.
3. The build cleans previous build files.
4. The size of `assets.bin` is bounded by the SPIFFS partition size.
