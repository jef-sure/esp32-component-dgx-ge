"""
A bitmap font of DGX (the C source font2c writes, or one bundled with DGX) as
JSON, for tools that measure and show text as the engine will (the editor's
preview, dgx_ge_text.py):

    {"name", "yAdvance", "yOffsetLowest", "xWidest", "f_type",
     "glyphs": {"<code point>": {"w", "h", "xAdvance", "xOffset", "yOffset", "rows": ["#.#..", ...]}}}

rows: the pixels of the glyph, top to bottom, '#' set. A handwritten font
(DGX_FONT_HW) is not read here: its own JSON (the hw-fonts editor) is.

    python3 dgx_ge_font.py Font.c [out.json]
"""

import json
import re
import sys


def _numbers(text):
    return [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|-?\d+", text)]


def read_font(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    # the bitmaps: the bytes, comments aside
    m = re.search(r"bitmaps\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise ValueError(f"{path}: no bitmaps")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    bitmaps = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", body)]
    m = re.search(r"glyph_t\s+glyphs\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise ValueError(f"{path}: no glyphs")
    glyphs = [tuple(int(v) for v in g) for g in
              re.findall(r"\{\s*\{\s*(?:\.bitmap\s*=\s*)?bitmaps\s*\+\s*(\d+)\s*\}\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}",
                         re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S))]
    m = re.search(r"glyph_array_t\s+glyph_ranges\[\]\s*=\s*\{(.*?)\};", src, re.S)
    ranges = [(int(a, 0), int(n, 0), int(k)) for a, n, k in re.findall(r"\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*glyphs\s*\+\s*(\d+)\s*\}",
                                                                       m.group(1))]
    fields = dict(re.findall(r"\.(\w+)\s*=\s*([-\w.]+)", src[src.rindex("dgx_font_t"):]))
    f_type = fields.get("f_type", "DGX_FONT_BITMAP_LINES")
    stream = f_type == "DGX_FONT_BITMAP_STREAM"
    font = {"name": re.search(r"dgx_font_t\s*\*\s*(\w+)\s*\(", src).group(1), "f_type": f_type,
            "yAdvance": int(fields["yAdvance"]), "yOffsetLowest": int(fields["yOffsetLowest"]),
            "xWidest": int(float(fields.get("xWidest", 0))), "glyphs": {}, "first_advance": glyphs[ranges[0][2]][3] if ranges else 0}
    for first, number, k in ranges:
        for i in range(number):
            at, w, h, adv, xo, yo = glyphs[k + i]
            rows = []
            for y in range(h):
                row = ""
                for x in range(w):
                    bit = (y * w + x) if stream else y * 8 * ((w + 7) // 8) + x
                    byte = bitmaps[at + bit // 8] if at + bit // 8 < len(bitmaps) else 0
                    row += "#" if byte >> (7 - bit % 8) & 1 else "."
                rows.append(row)
            font["glyphs"][str(first + i)] = {"w": w, "h": h, "xAdvance": adv, "xOffset": xo, "yOffset": yo, "rows": rows}
    return font


if __name__ == "__main__":
    font = read_font(sys.argv[1])
    text = json.dumps(font, ensure_ascii=False)
    if len(sys.argv) > 2:
        open(sys.argv[2], "w", encoding="utf-8").write(text)
    else:
        print(text)
