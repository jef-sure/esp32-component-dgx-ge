"""dgx_ge_text.py lays a text as the engine does, and dgx_ge_font.py reads the font as DGX draws it: the same lines, the same points."""
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from dgx_ge_font import read_font  # noqa: E402
from dgx_ge_text import layout  # noqa: E402

DGX = os.environ.get("DGX", os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "managed_components", "jef-sure__dgx"))
FONTS = {"terminus": read_font(os.path.join(DGX, "src", "fonts", "TerminusTTFMedium12.c")),
         "arial": read_font(os.path.join(DGX, "src", "fonts", "ArialRegular12.c"))}  # monospace, and proportional
failed = []


def canvas_of(lay, width, height, font):
    """The points of the lines shown, as DGX draws glyphs: each at its offsets, a pixel a scale x scale block."""
    width = (width + 7) & ~7
    px = [[0] * width for _ in range(height)]
    s, line, above = lay["scale"], font["yAdvance"] * lay["scale"], -font["yOffsetLowest"] * lay["scale"]
    texts = lay["lines"][:lay["shown"]]
    if lay["cut"] and texts:
        texts[-1] = lay["last"]
    for k, t in enumerate(texts):
        x, base = 0, k * line + above
        for ch in t:
            g = font["glyphs"].get(str(ord(ch)))
            if not g:
                x += font["first_advance"] * s
                continue
            for gy, row in enumerate(g["rows"]):
                for gx, c in enumerate(row):
                    if c != "#":
                        continue
                    for dy in range(s):
                        for dx in range(s):
                            X, Y = x + (g["xOffset"] + gx) * s + dx, base + (g["yOffset"] + gy) * s + dy
                            if 0 <= X < width and 0 <= Y < height:
                                px[Y][X] = 1
            x += g["xAdvance"] * s
    out = bytearray()
    for row in px:
        for b in range(0, width, 8):
            out.append(sum(row[b + i] << (7 - i) for i in range(8)))
    return out.hex()


cases = [(64, 26, 2, 1, 0, "Hi"), (64, 26, 2, 1, 0, "Стой! Кто идёт?"), (64, 26, 2, 1, 0, "a\nb"), (64, 26, 2, 1, 0, "abcdefghijklmn"),
         (64, 26, 2, 1, 0, "one two three four five six seven eight nine ten"), (64, 26, 2, 1, 1, "one two three four five six seven"),
         (64, 26, 2, 1, 0, "one two abcdef hi three four"), (100, 40, 3, 1, 0, "Угощайся, это вино с юга."), (60, 12, 2, 2, 0, "long words here"),
         (200, 13, 1, 1, 0, "Да здравствует мыло душистое и полотенце пушистое"), (40, 26, 1, 1, 1, "Ёжик в тумане и лошадка"),
         (16, 30, 1, 1, 0, "x\n    yy"),  # a line of spaces first, cut down to nothing: the ellipsis alone
         (64, 26, 2, 1, 0, "Hi\n"), (64, 26, 2, 1, 0, "Hi \n")]  # a line break after the last line: as without it
cases = [c + ("terminus",) for c in cases] + [c + ("arial",) for c in cases]
binary = sys.argv[1] if len(sys.argv) > 1 else "build/text_lay"
for w, h, hi, lo, imp, text, name in cases:
    font = FONTS[name]
    out = subprocess.run([binary, str(w), str(h), str(hi), str(lo), str(imp), text, name], capture_output=True, text=True).stdout.split("\n")
    head, canvas = out[0].split(), out[1].split()[1]
    lay = layout(text, font, w, h, hi, lo, bool(imp))
    data = text.encode()
    c_lines = [data[int(a):int(b)].decode() for a, b in (r.split("-") for r in head[9:])]
    if (int(head[1]), int(head[3]), bool(int(head[5])), bool(int(head[7]))) != (lay["scale"], lay["shown"], lay["cut"], lay["scrolling"]):
        failed.append(f"{text!r}: engine {head[:8]} tool {lay}")
    if c_lines != lay["lines"]:
        failed.append(f"{text!r}: lines engine {c_lines} tool {lay['lines']}")
    if canvas != canvas_of(lay, w, h, font):
        failed.append(f"{text!r} ({name}): the points differ")
for name in FONTS:
    if layout("Hi \n", FONTS[name], 64, 26, 2, 1)["scale"] != layout("Hi", FONTS[name], 64, 26, 2, 1)["scale"]:
        failed.append(f"{name}: a line break at the end lays it smaller")
print("test_text_layout.py: ok" if not failed else "test_text_layout.py: " + "\n".join(failed))
sys.exit(1 if failed else 0)
