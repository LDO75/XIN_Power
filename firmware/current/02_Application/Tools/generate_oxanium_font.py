#!/usr/bin/env python3
"""将 Oxanium 的界面字符转换为 LCD 驱动使用的 4bpp 抗锯齿字模。"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import math


PROJECT_ROOT = Path(__file__).resolve().parents[1]
FONT_PATH = PROJECT_ROOT / "ThirdParty" / "Oxanium" / "Oxanium-wght.ttf"
OUTPUT_PATH = PROJECT_ROOT / "Core" / "SRC" / "lcd_font_oxanium.inc"

FONT_CONFIGS = (
    ("small", 13, "Medium", " !%()+-./0123456789:?ABCDEFGHIJKLMNOPQRSTUVWXYZ^"),
    ("medium", 16, "Medium", " !%+-./0123456789:?ABCDEFGHIJKLMNOPQRSTUVWXYZ"),
    ("large", 32, "SemiBold", " +-.0123456789?AV"),
    ("value", 36, "SemiBold", " +-.0123456789?AV"),
)


def load_font(size: int, variation: str) -> ImageFont.FreeTypeFont:
    """加载指定字号和字重的可变字体实例。"""
    font = ImageFont.truetype(str(FONT_PATH), size)
    font.set_variation_by_name(variation)
    return font


def pack_nibbles(alpha_values: list[int]) -> list[int]:
    """把 0～15 的透明度数据按每字节两个像素进行打包。"""
    if len(alpha_values) & 1:
        alpha_values.append(0)
    return [
        (alpha_values[index] << 4) | alpha_values[index + 1]
        for index in range(0, len(alpha_values), 2)
    ]


def generate_font(name: str, size: int, variation: str, characters: str) -> tuple[str, int]:
    """生成一套字号的字形表、位图表和字体描述结构。"""
    font = load_font(size, variation)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    glyph_rows: list[str] = []
    bitmap_bytes: list[int] = []

    for character in characters:
        left, top, right, bottom = font.getbbox(character)
        width = max(0, right - left)
        height = max(0, bottom - top)
        advance = max(1, math.ceil(font.getlength(character)) + 1)
        bitmap_offset = len(bitmap_bytes)

        if width and height:
            image = Image.new("L", (width, height), 0)
            drawer = ImageDraw.Draw(image)
            drawer.text((-left, -top), character, font=font, fill=255)
            pixel_values = (
                image.get_flattened_data()
                if hasattr(image, "get_flattened_data")
                else image.getdata()
            )
            alpha = [(value + 8) // 17 for value in pixel_values]
            packed = pack_nibbles(alpha)
            bitmap_bytes.extend(packed)
            byte_count = len(packed)
        else:
            byte_count = 0

        glyph_rows.append(
            "  {%dU, %dU, %d, %d, %dU, %dU, %dU}, /* 0x%02X '%s' */"
            % (
                width,
                height,
                left,
                top,
                advance,
                bitmap_offset,
                byte_count,
                ord(character),
                character.replace("'", "\\'"),
            )
        )

    bitmap_rows = []
    for index in range(0, len(bitmap_bytes), 16):
        bitmap_rows.append(
            "  " + ", ".join(f"0x{value:02X}U" for value in bitmap_bytes[index:index + 16]) + ","
        )

    escaped_chars = characters.replace("\\", "\\\\").replace('"', '\\"')
    block = f"""
static const char s_oxanium_{name}_characters[] = \"{escaped_chars}\";

static const LCD_FontGlyph_t s_oxanium_{name}_glyphs[] =
{{
{chr(10).join(glyph_rows)}
}};

static const uint8_t s_oxanium_{name}_bitmap[] =
{{
{chr(10).join(bitmap_rows)}
}};

static const LCD_Font_t s_oxanium_{name}_font =
{{
  s_oxanium_{name}_characters,
  s_oxanium_{name}_glyphs,
  s_oxanium_{name}_bitmap,
  {len(characters)}U,
  {line_height}U,
  1U
}};
"""
    return block, len(bitmap_bytes)


def main() -> None:
    """生成 C include 文件并输出字模空间统计。"""
    blocks = []
    total_bytes = 0
    for config in FONT_CONFIGS:
        block, byte_count = generate_font(*config)
        blocks.append(block)
        total_bytes += byte_count

    header = """/*
 * Oxanium 嵌入式 4bpp 抗锯齿字模。
 * Copyright 2019 The Oxanium Project Authors.
 * 本衍生字模依据 SIL Open Font License 1.1 分发；完整许可见
 * ThirdParty/Oxanium/OFL.txt。
 * 本文件由 Tools/generate_oxanium_font.py 自动生成，请勿手工修改。
 */
"""
    OUTPUT_PATH.write_text(header + "".join(blocks), encoding="utf-8")
    print(f"generated: {OUTPUT_PATH}")
    print(f"bitmap bytes: {total_bytes}")


if __name__ == "__main__":
    main()
