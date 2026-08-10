#!/usr/bin/env python3
"""Generate the 240x135 pixel-art project image and its RGB565 RLE header."""

from pathlib import Path
from PIL import Image, ImageDraw

WIDTH, HEIGHT = 240, 135
ROOT = Path(__file__).resolve().parent
PNG_PATH = ROOT / "assets" / "logo.png"
HEADER_PATH = ROOT / "components" / "ui" / "src" / "splash_art.h"

COLORS_565 = {
    "ink": 0x0841, "deep": 0x1084, "sky": 0x18C7, "panel": 0x212A,
    "grid": 0x2290, "cyan_dark": 0x04D2, "cyan": 0x07FF,
    "green": 0x07E0, "green_dim": 0x03C8, "white": 0xFFFF,
    "silver": 0xBDF7, "gray": 0x632C, "red": 0xF9E7,
    "orange": 0xFD20, "yellow": 0xFFE0, "blue": 0x255F,
}

FONT = {
    "A": ("01110","10001","10001","11111","10001","10001","10001"),
    "B": ("11110","10001","10001","11110","10001","10001","11110"),
    "C": ("01111","10000","10000","10000","10000","10000","01111"),
    "D": ("11110","10001","10001","10001","10001","10001","11110"),
    "E": ("11111","10000","10000","11110","10000","10000","11111"),
    "F": ("11111","10000","10000","11110","10000","10000","10000"),
    "G": ("01111","10000","10000","10111","10001","10001","01111"),
    "H": ("10001","10001","10001","11111","10001","10001","10001"),
    "I": ("11111","00100","00100","00100","00100","00100","11111"),
    "J": ("00111","00010","00010","00010","10010","10010","01100"),
    "K": ("10001","10010","10100","11000","10100","10010","10001"),
    "L": ("10000","10000","10000","10000","10000","10000","11111"),
    "M": ("10001","11011","10101","10101","10001","10001","10001"),
    "N": ("10001","11001","10101","10011","10001","10001","10001"),
    "O": ("01110","10001","10001","10001","10001","10001","01110"),
    "P": ("11110","10001","10001","11110","10000","10000","10000"),
    "Q": ("01110","10001","10001","10001","10101","10010","01101"),
    "R": ("11110","10001","10001","11110","10100","10010","10001"),
    "S": ("01111","10000","10000","01110","00001","00001","11110"),
    "T": ("11111","00100","00100","00100","00100","00100","00100"),
    "U": ("10001","10001","10001","10001","10001","10001","01110"),
    "V": ("10001","10001","10001","10001","10001","01010","00100"),
    "W": ("10001","10001","10001","10101","10101","10101","01010"),
    "X": ("10001","10001","01010","00100","01010","10001","10001"),
    "Y": ("10001","10001","01010","00100","00100","00100","00100"),
    "Z": ("11111","00001","00010","00100","01000","10000","11111"),
    "0": ("01110","10001","10011","10101","11001","10001","01110"),
    "1": ("00100","01100","00100","00100","00100","00100","01110"),
    "2": ("01110","10001","00001","00010","00100","01000","11111"),
    "3": ("11110","00001","00001","01110","00001","00001","11110"),
    "4": ("00010","00110","01010","10010","11111","00010","00010"),
    "5": ("11111","10000","10000","11110","00001","00001","11110"),
    "6": ("01110","10000","10000","11110","10001","10001","01110"),
    "7": ("11111","00001","00010","00100","01000","01000","01000"),
    "8": ("01110","10001","10001","01110","10001","10001","01110"),
    "9": ("01110","10001","10001","01111","00001","00001","01110"),
    "/": ("00001","00010","00010","00100","01000","01000","10000"),
    ":": ("00000","00100","00100","00000","00100","00100","00000"),
    "-": ("00000","00000","00000","11111","00000","00000","00000"),
    ".": ("00000","00000","00000","00000","00000","00110","00110"),
    " ": ("00000",) * 7,
}


def rgb565_to_rgb(value):
    r = ((value >> 11) & 0x1F) * 255 // 31
    g = ((value >> 5) & 0x3F) * 255 // 63
    b = (value & 0x1F) * 255 // 31
    return r, g, b


C = {name: rgb565_to_rgb(value) for name, value in COLORS_565.items()}

def text_width(text, scale=1):
    return max(0, (len(text) * 6 - 1) * scale)


def pixel_text(draw, xy, text, color, scale=1):
    x, y = xy
    for char in text.upper():
        glyph = FONT.get(char, FONT[" "])
        for row, bits in enumerate(glyph):
            for col, bit in enumerate(bits):
                if bit == "1":
                    px, py = x + col * scale, y + row * scale
                    draw.rectangle((px, py, px + scale - 1, py + scale - 1), fill=color)
        x += 6 * scale


def draw_art():
    image = Image.new("RGB", (WIDTH, HEIGHT), C["ink"])
    draw = ImageDraw.Draw(image)

    # Layered night sky and a sparse digital star field.
    draw.rectangle((0, 18, 239, 76), fill=C["deep"])
    draw.rectangle((0, 54, 239, 95), fill=C["sky"])
    stars = ((8,25),(18,44),(33,19),(48,32),(79,24),(96,39),(108,17),
             (131,22),(151,14),(170,27),(193,19),(218,33),(231,16),(226,58))
    for index, (x, y) in enumerate(stars):
        color = C["cyan"] if index % 4 == 0 else C["silver"]
        draw.rectangle((x, y, x + (index % 3 == 0), y + (index % 3 == 0)), fill=color)

    # HUD frame, horizon, and retro perspective grid.
    draw.rectangle((0, 0, 239, 2), fill=C["cyan"])
    draw.rectangle((0, 132, 239, 134), fill=C["cyan"])
    draw.rectangle((3, 6, 5, 15), fill=C["green"])
    draw.rectangle((6, 6, 15, 8), fill=C["green"])
    draw.rectangle((224, 6, 233, 8), fill=C["green"])
    draw.rectangle((234, 6, 236, 15), fill=C["green"])
    draw.line((0, 96, 239, 96), fill=C["cyan_dark"], width=2)
    for y in (101, 108, 117, 129):
        draw.line((0, y, 239, y), fill=C["grid"])
    for x in range(-180, 421, 30):
        draw.line((120, 96, x, 131), fill=C["grid"])

    # Pixel radar scope behind the aircraft.
    draw.ellipse((19, 17, 101, 99), outline=C["cyan_dark"], width=2)
    draw.ellipse((29, 27, 91, 89), outline=C["grid"], width=1)
    draw.line((60, 17, 60, 99), fill=C["grid"])
    draw.line((19, 58, 101, 58), fill=C["grid"])
    draw.line((60, 58, 91, 35), fill=C["green_dim"], width=2)
    draw.rectangle((91, 34, 94, 37), fill=C["green"])

    # Four-rotor drone silhouette, viewed from above.
    for x, y in ((35,33),(85,33),(35,83),(85,83)):
        draw.ellipse((x-11, y-5, x+11, y+5), outline=C["orange"], width=2)
        draw.rectangle((x-3, y-3, x+3, y+3), fill=C["red"])
    draw.line((38,36,82,80), fill=C["gray"], width=7)
    draw.line((82,36,38,80), fill=C["gray"], width=7)
    draw.line((38,36,82,80), fill=C["cyan"], width=3)
    draw.line((82,36,38,80), fill=C["cyan"], width=3)
    draw.polygon(((52,44),(68,44),(76,58),(68,75),(52,75),(44,58)), fill=C["panel"])
    draw.polygon(((56,47),(64,47),(70,58),(64,69),(56,69),(50,58)), fill=C["silver"])
    draw.rectangle((56,53,64,62), fill=C["blue"])
    draw.rectangle((58,55,62,60), fill=C["green"])
    draw.rectangle((42,56,46,60), fill=C["white"])
    draw.rectangle((74,56,78,60), fill=C["white"])

    # Signal arcs and title block.
    draw.arc((13, 10, 107, 106), 205, 255, fill=C["green"], width=2)
    draw.arc((10, 7, 110, 109), 285, 335, fill=C["green"], width=2)
    draw.rectangle((113, 25, 116, 78), fill=C["cyan_dark"])
    draw.rectangle((113, 25, 125, 27), fill=C["cyan"])
    pixel_text(draw, (123, 30), "DRONE", C["white"], scale=3)
    pixel_text(draw, (123, 55), "TOOLS", C["green"], scale=3)
    pixel_text(draw, (124, 80), "/ TELEMETRY MONITOR", C["silver"])
    draw.rectangle((123, 91, 225, 92), fill=C["cyan_dark"])
    draw.rectangle((123, 91, 164, 92), fill=C["cyan"])

    # Bottom instrument strip.
    draw.rectangle((5, 108, 234, 127), fill=C["panel"])
    draw.rectangle((5, 108, 234, 109), fill=C["cyan_dark"])
    draw.rectangle((10, 114, 13, 117), fill=C["green"])
    pixel_text(draw, (17, 113), "SIGNAL ONLINE", C["green"])
    pixel_text(draw, (116, 113), "ESP32-S3", C["silver"])
    for x, height, color in ((177,3,"green_dim"),(182,5,"green_dim"),
                             (187,7,"green"),(192,10,"green")):
        draw.rectangle((x, 122-height, x+2, 122), fill=C[color])
    draw.rectangle((206, 114, 228, 121), outline=C["silver"])
    draw.rectangle((229, 116, 231, 119), fill=C["silver"])
    draw.rectangle((208, 116, 223, 119), fill=C["yellow"])
    return image


def rgb_to_565(rgb):
    r, g, b = rgb
    return (((r * 31 + 127) // 255) << 11) | (((g * 63 + 127) // 255) << 5) | ((b * 31 + 127) // 255)


def encode_runs(image):
    raw = image.tobytes()
    values = [rgb_to_565(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    runs = []
    current, length = values[0], 1
    for value in values[1:]:
        if value == current and length < 0xFFFF:
            length += 1
        else:
            runs.append((length, current))
            current, length = value, 1
    runs.append((length, current))
    return runs


def write_header(runs):
    lines = [
        "/* Generated by generate_logo.py. Do not edit manually. */",
        "#pragma once", "", "#include <stdint.h>", "",
        f"#define SPLASH_ART_WIDTH {WIDTH}",
        f"#define SPLASH_ART_HEIGHT {HEIGHT}",
        f"#define SPLASH_ART_RUN_COUNT {len(runs)}", "",
        "typedef struct {", "    uint16_t length;", "    uint16_t color;",
        "} splash_art_run_t;", "",
        "static const splash_art_run_t s_splash_art_runs[SPLASH_ART_RUN_COUNT] = {",
    ]
    for start in range(0, len(runs), 6):
        chunk = runs[start:start + 6]
        lines.append("    " + ", ".join(f"{{{n}, 0x{c:04X}}}" for n, c in chunk) + ",")
    lines.extend(("};", ""))
    HEADER_PATH.write_text("\n".join(lines), encoding="utf-8")


def main():
    PNG_PATH.parent.mkdir(parents=True, exist_ok=True)
    image = draw_art()
    image.save(PNG_PATH, "PNG", optimize=True)
    runs = encode_runs(image)
    write_header(runs)
    raw_bytes = WIDTH * HEIGHT * 2
    rle_bytes = len(runs) * 4
    print(f"Generated {PNG_PATH.relative_to(ROOT)} ({WIDTH}x{HEIGHT})")
    print(f"Generated {HEADER_PATH.relative_to(ROOT)} ({len(runs)} runs, {rle_bytes}/{raw_bytes} bytes)")


if __name__ == "__main__":
    main()
