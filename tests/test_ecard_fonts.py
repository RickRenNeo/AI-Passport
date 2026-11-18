#!/usr/bin/env python3
"""Gate the eCard font assets against the UI's real character inventory."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

import tools.generate_ecard_fonts as fonts  # noqa: E402

FONT_DIR = REPO_ROOT / "assets" / "fonts"
CMAKE = REPO_ROOT / "main" / "CMakeLists.txt"
SDKCONFIG = REPO_ROOT / "sdkconfig.defaults"
UI_SOURCE = REPO_ROOT / "main" / "ecard_ui.c"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_generated(size: int) -> tuple[str, set[int]]:
    path = FONT_DIR / f"ecard_font_{size}.c"
    require(path.is_file(), f"missing generated font: {path.relative_to(REPO_ROOT)}")
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError as error:
        raise AssertionError(f"{path.name} is not valid UTF-8: {error}") from error
    return text, fonts.generated_codepoints(path)


def check_coverage(size: int, required: set[str]) -> None:
    text, actual = read_generated(size)
    symbol = re.search(rf"const lv_font_t (ecard_font_{size})\b", text)
    require(symbol is not None, f"ecard_font_{size}.c does not define the expected LVGL 9 symbol")
    require(".fallback = NULL" in text, f"ecard_font_{size}.c has no fallback field")
    require(".bitmap_format = 1" in text, f"ecard_font_{size}.c must use LVGL RLE compression (.bitmap_format = 1)")

    missing = sorted(ch for ch in required if ord(ch) not in actual)
    if missing:
        preview = " ".join(f"U+{ord(ch):04X}({ch})" for ch in missing[:20])
        raise AssertionError(
            f"ecard_font_{size}.c is missing {len(missing)} required glyph(s): {preview}"
        )

    require(not any(ord(ch) in actual for ch in "\n\r\t"), f"ecard_font_{size}.c contains control glyphs")


def check_negative_case() -> None:
    # U+9F98 (龘) is outside the UI's literal inventory. It must stay absent so a
    # coverage check cannot pass by claiming the whole CJK block.
    require("\u9f98" not in fonts.REQUIRED_SYMBOLS, "negative probe must not be a required symbol")
    require("\u9f98" not in fonts.application_chars(), "negative probe leaked into UI sources")
    for size in fonts.SIZES:
        _, actual = read_generated(size)
        require(0x9F98 not in actual, f"ecard_font_{size}.c unexpectedly contains U+9F98")


def check_build_integration() -> None:
    cmake = CMAKE.read_text(encoding="utf-8")
    for size in fonts.SIZES:
        name = f"ecard_font_{size}.c"
        require(name in cmake, f"main/CMakeLists.txt does not compile {name}")

    config = SDKCONFIG.read_text(encoding="utf-8")
    for setting in (
        "CONFIG_LV_TXT_ENC_UTF8=y",
        "CONFIG_LV_USE_FONT_COMPRESSED=y",
        "CONFIG_LV_USE_FONT_PLACEHOLDER=y",
        "CONFIG_LV_FONT_FMT_TXT_LARGE=y",
    ):
        require(setting in config, f"sdkconfig.defaults is missing {setting}")

    ui = UI_SOURCE.read_text(encoding="utf-8")
    declared = {int(value) for value in re.findall(r"LV_FONT_DECLARE\(ecard_font_(\d+)\)", ui)}
    require(declared == set(fonts.SIZES), f"ecard_ui.c declares fonts {sorted(declared)}, expected {list(fonts.SIZES)}")


def main() -> int:
    ui_chars = fonts.application_chars() | set(fonts.REQUIRED_SYMBOLS)
    gb2312 = fonts.gb2312_level1()
    require(len(gb2312) == 3755, f"GB2312 level-1 inventory changed: {len(gb2312)}")

    for size in fonts.SIZES:
        if size in fonts.ASCII_ONLY_SIZES:
            required = set()
        else:
            required = set(ui_chars)
            if size in fonts.GB2312_SIZES:
                required |= gb2312
        check_coverage(size, required)
        print(f"ecard_font_{size}.c: {len(required)} required glyphs present")

    check_negative_case()
    check_build_integration()
    print("ecard font coverage: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
