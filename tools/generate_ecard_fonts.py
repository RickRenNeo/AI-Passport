#!/usr/bin/env python3
"""Generate the LVGL bitmap fonts used by the eCard UI.

Source font
    Noto Sans CJK SC Regular, pinned to the noto-cjk ``Sans2.004`` tag.
    License: SIL Open Font License 1.1 (see assets/fonts/LICENSE-NotoSansCJK.txt).

Converter
    lv_font_conv 1.5.3 (https://github.com/lvgl/lv_font_conv), run with
    ``--no-compress`` so the generated fonts do not depend on
    CONFIG_LV_USE_FONT_COMPRESSED.

Character inventory
    * printable ASCII;
    * every non-ASCII character in the eCard application sources;
    * explicit UI symbols (stars, arrows, punctuation);
    * GB2312 level-1 hanzi for the body/title sizes that render dynamic
      profile and network text (12, 16 and 22 px).

Characters outside this inventory are unsupported and LVGL will draw its
missing-glyph placeholder. Extend the source set and rerun this script, then
run tests/test_ecard_fonts.py.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = REPO_ROOT / "assets" / "fonts"
CACHE_DIR = FONT_DIR / ".cache"

CONVERTER_VERSION = "1.5.3"
SOURCE_TAG = "Sans2.004"
SOURCE_URL = (
    "https://github.com/notofonts/noto-cjk/raw/"
    f"{SOURCE_TAG}/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf"
)
SOURCE_SHA256 = "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"
SOURCE_FILENAME = "NotoSansCJKsc-Regular.otf"

SIZES = (9, 12, 16, 22, 48)
# 2 bpp keeps four antialiasing levels: readable at 240x320 while halving the
# bitmap payload compared with 4 bpp. Verified against every screen render.
BPP = 2
# Sizes used only for Latin text or digits: including CJK here would add
# megabytes of glyphs that no widget ever selects.
ASCII_ONLY_SIZES = frozenset({9, 48})
# The subset is built from the UI's real literals plus GB2312 level-1 for the
# body/title sizes. School, class and student name are entered by the user at
# provisioning time, so a literals-only subset would render missing-glyph boxes
# for perfectly ordinary names. 9/48 px stay literal/ASCII-only because they
# never render user input.
GB2312_SIZES = frozenset({12, 16, 22})
TEXT_SOURCES = (
    "main/ecard_ui.c",
    "main/ecard_app.c",
    "main/ecard_profile.c",
    "main/ecard_quiz.c",
    "main/ecard_cards.c",
    "main/ecard_flashcard.c",
    "main/ecard_network.c",
    "main/ecard_pomodoro.c",
    "main/ecard_voice.c",
)
# Symbols that must survive future text edits even if no current literal uses
# them. Keep this list small and verify it in tests/test_ecard_fonts.py.
REQUIRED_SYMBOLS = "·—…“”‘’《》〈〉、。，；：！？（）【】￥★▶✓›"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def gb2312_level1() -> set[str]:
    chars: set[str] = set()
    for high in range(0xB0, 0xD8):
        for low in range(0xA1, 0xFF):
            try:
                chars.add(bytes((high, low)).decode("gb2312"))
            except UnicodeDecodeError:
                continue
    return chars


def application_chars() -> set[str]:
    chars: set[str] = set()
    for relative in TEXT_SOURCES:
        path = REPO_ROOT / relative
        if not path.exists():
            raise SystemExit(f"missing source file: {relative}")
        for char in path.read_text(encoding="utf-8"):
            if ord(char) >= 0x80:
                chars.add(char)
    return chars


def resolve_converter(explicit: str | None) -> str:
    candidates = []
    if explicit:
        candidates.append(Path(explicit).expanduser())
    on_path = shutil.which("lv_font_conv")
    if on_path:
        candidates.append(Path(on_path))
    # Local development fallback used by the font-generation documentation.
    candidates.append(Path("/tmp/lvfont/node_modules/.bin/lv_font_conv"))
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise SystemExit(
        "lv_font_conv not found. Install the pinned converter:\n"
        f"  npm install --prefix /tmp/lvfont lv_font_conv@{CONVERTER_VERSION}\n"
        "then rerun with --converter /tmp/lvfont/node_modules/.bin/lv_font_conv"
    )


def resolve_source_font(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit).expanduser()
        if not path.is_file():
            raise SystemExit(f"font not found: {path}")
    else:
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        path = CACHE_DIR / SOURCE_FILENAME
        if not path.exists():
            print(f"downloading {SOURCE_URL}")
            urllib.request.urlretrieve(SOURCE_URL, path)

    digest = sha256_file(path)
    if digest != SOURCE_SHA256:
        raise SystemExit(
            f"unexpected SHA-256 for {path}:\n  expected {SOURCE_SHA256}\n"
            f"  actual   {digest}"
        )
    return path


def generated_codepoints(path: Path) -> set[int]:
    """Read the U+XXXX comments emitted by lv_font_conv for every glyph."""
    codepoints: set[int] = set()
    text = path.read_text(encoding="utf-8")
    marker = "/* U+"
    start = 0
    while True:
        index = text.find(marker, start)
        if index < 0:
            break
        end = text.find(" ", index + len(marker))
        if end < 0:
            break
        codepoints.add(int(text[index + len(marker):end], 16))
        start = end
    return codepoints


def generate_size(converter: str, font: Path, size: int, symbols: set[str]) -> None:
    output = FONT_DIR / f"ecard_font_{size}.c"
    command = [
        converter,
        "--font", str(font),
        "--range", "0x20-0x7E",
        "--symbols", "".join(sorted(symbols)),
        "--size", str(size),
        "--bpp", str(BPP),
        "--format", "lvgl",
        "--no-compress",
        "--lv-font-name", f"ecard_font_{size}",
        "--lv-include", "lvgl.h",
        "--output", str(output),
    ]
    print(f"generating {output.name}: {len(symbols)} symbols, {size} px")
    subprocess.run(command, check=True)

    wanted = set(range(0x20, 0x7F)) | {ord(char) for char in symbols}
    actual = generated_codepoints(output)
    missing = sorted(wanted - actual)
    if missing:
        preview = " ".join(f"U+{cp:04X}" for cp in missing[:20])
        raise SystemExit(
            f"{output.name}: source font is missing {len(missing)} requested "
            f"glyph(s): {preview}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--converter", help="path to lv_font_conv")
    parser.add_argument("--font", help=f"path to {SOURCE_FILENAME}")
    args = parser.parse_args()

    converter = resolve_converter(args.converter)
    font = resolve_source_font(args.font)

    FONT_DIR.mkdir(parents=True, exist_ok=True)
    ui_chars = application_chars() | set(REQUIRED_SYMBOLS)
    print(f"application/UI characters: {len(ui_chars)}")

    for size in SIZES:
        symbols = set() if size in ASCII_ONLY_SIZES else set(ui_chars)
        if size in GB2312_SIZES:
            symbols |= gb2312_level1()
        generate_size(converter, font, size, symbols)

    print(
        f"done with lv_font_conv {CONVERTER_VERSION}; "
        f"source font {SOURCE_TAG} sha256={SOURCE_SHA256}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
