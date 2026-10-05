"""Encode the supplied logo for the ST7789 as lossless RGB565 row RLE.

This is a deterministic build-asset conversion, not a generated redesign.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Assets" / "XIN_Power_logo_original.png"
DESTINATION = ROOT / "Core" / "SRC" / "splash_logo_data.c"
PREVIEW = ROOT / "Assets" / "splash_preview_320x240.png"
WIDTH, HEIGHT = 320, 240
DARK_BACKGROUND = (8, 19, 29)
LIGHT_LOGO = (224, 239, 242)
MINT_ACCENT = (49, 222, 177)


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def dark_theme_pixel(red: int, green: int, blue: int) -> tuple[int, int, int]:
    """Keep the supplied logo's shape, remapping white paper to dark UI."""
    if green - red > 15 and green - blue > 8:
        foreground = MINT_ACCENT
        opacity = min(1.0, max(0.0, (255 - red) / 205.0))
    else:
        foreground = LIGHT_LOGO
        opacity = 1.0 - (red + green + blue) / (3.0 * 255.0)
    # The supplied PNG has a faint paper texture. Suppress that near-white
    # texture so the logo does not sit inside a visible rectangular patch.
    opacity = max(0.0, (opacity - 0.10) / 0.90)
    return tuple(round(base + (ink - base) * opacity)
                 for base, ink in zip(DARK_BACKGROUND, foreground))


def main() -> None:
    source = Image.open(SOURCE).convert("RGB")
    points = source.load()
    xs: list[int] = []
    ys: list[int] = []
    for y in range(source.height):
        for x in range(source.width):
            if min(points[x, y]) < 190:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise ValueError("Logo foreground was not found")
    padding = 12
    bounds = (
        max(0, min(xs) - padding),
        max(0, min(ys) - padding),
        min(source.width, max(xs) + padding + 1),
        min(source.height, max(ys) + padding + 1),
    )
    logo = source.crop(bounds)
    logo.thumbnail((294, 180), Image.Resampling.LANCZOS)
    themed_logo = Image.new("RGB", logo.size)
    themed_logo.putdata([dark_theme_pixel(*pixel) for pixel in logo.getdata()])
    canvas = Image.new("RGB", (WIDTH, HEIGHT), DARK_BACKGROUND)
    canvas.paste(themed_logo,
                 ((WIDTH - themed_logo.width) // 2, (HEIGHT - themed_logo.height) // 2))
    canvas.save(PREVIEW)

    encoded = bytearray()
    pixels = canvas.load()
    for y in range(HEIGHT):
        x = 0
        while x < WIDTH:
            color = rgb565(*pixels[x, y])
            count = 1
            while x + count < WIDTH and count < 255 and rgb565(*pixels[x + count, y]) == color:
                count += 1
            encoded.extend((count, color & 0xFF, color >> 8))
            x += count

    lines = [
        "/* Generated from Assets/XIN_Power_logo_original.png; do not hand-edit. */",
        '#include "splash_logo.h"',
        "const uint8_t g_splash_logo_rle[] = {",
    ]
    for index in range(0, len(encoded), 18):
        chunk = encoded[index:index + 18]
        lines.append("  " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")
    lines += [
        "};",
        f"const uint32_t g_splash_logo_rle_size = {len(encoded)}UL;",
        "",
    ]
    DESTINATION.write_text("\n".join(lines), encoding="ascii")
    print(f"logo bounds={bounds}, display={logo.size}, encoded={len(encoded)} bytes")


if __name__ == "__main__":
    main()
