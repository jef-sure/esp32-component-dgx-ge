"""
The layout of a field of text exactly as the engine makes it
(src/dgx_ge_text.c), for tools that must show the same lines: words into
lines (a word wider than a line by characters, '\\n' breaks), the largest
scale of the font that fits but not below the style's least; still too
much: an important text scrolls, another is cut, its last line seen ending
in "...". The font as dgx_ge_font.py reads it. Bytes of UTF-8 throughout, as
the engine counts them.
"""

LINE_BYTES = 160
ELLIPSIS = b"..."


def width_of(data, font, scale):
    """How wide bytes of UTF-8 are in a font at a scale (as dgx_font_string_bounds: a missing glyph as wide as the first one)."""
    data = data[:LINE_BYTES]
    width = 0
    for ch in data.decode("utf-8", errors="ignore"):
        g = font["glyphs"].get(str(ord(ch)))
        width += g["xAdvance"] if g else font["first_advance"]
    return width * scale


def _next_char(data, i):
    i += 1
    while i < len(data) and (data[i] & 0xC0) == 0x80:
        i += 1
    return i


def lay(data, font, scale, width, lines_max=0):
    """Lines of bytes of UTF-8 as wide as width at a scale: [(start, end)], at most lines_max of them (0: any number)."""
    lines, i, n = [], 0, len(data)
    while i < n and (lines_max <= 0 or len(lines) < lines_max):
        start = end = i
        while True:
            w = end
            while w < n and data[w] == 0x20:
                w += 1
            e = w
            while e < n and data[e] not in (0x20, 0x0A):
                e += 1
            if e - start > LINE_BYTES or width_of(data[start:e], font, scale) > width:
                if end == start:
                    c = start
                    while c < e and width_of(data[start:_next_char(data, c)], font, scale) <= width and _next_char(data, c) - start <= LINE_BYTES:
                        c = _next_char(data, c)
                    end = c if c > start else _next_char(data, start)
                break
            end = e
            if e >= n or data[e] == 0x0A:
                break
        lines.append((start, end))
        i = end
        while i < n and data[i] == 0x20:
            i += 1
        if i < n and data[i] == 0x0A:
            i += 1
    return lines


def layout(text, font, width, height, scale_max=1, scale_min=1, important=False, lines_max=0):
    """
    The field as the engine lays it: {"scale", "lines": [str], "shown",
    "cut", "scrolling", "last": the last line seen as drawn (with "..." when cut)}.
    width as the engine rounds it (a whole number of bytes); lines_max as the
    box was made (0: any number).
    """
    data = text.encode("utf-8")
    width = (width + 7) & ~7
    lo = scale_min or 1
    hi = scale_max if scale_max > lo else lo
    scale, lines, fits = hi, [], 0
    while scale >= lo:
        lines = lay(data, font, scale, width, lines_max)
        fits = height // (font["yAdvance"] * scale)
        if not lines or (len(lines) <= fits and not data[lines[-1][1]:].strip(b" \n")):  # spaces and line breaks after it: no text
            break
        scale -= 1
    if scale < lo:
        scale = lo
        lines = lay(data, font, scale, width, lines_max)
        fits = height // (font["yAdvance"] * scale)
    shown = min(fits, len(lines))
    unlaid = bool(lines and data[lines[-1][1]:].strip(b" \n"))  # the box had no lines for the rest: cut whatever its importance
    cut = unlaid or (len(lines) > fits and not important)
    scrolling = not cut and len(lines) > fits
    if not shown and lines:
        shown = 1
    texts = [data[a:b][:LINE_BYTES] for a, b in lines]
    last = texts[shown - 1] if shown else b""
    if cut and shown:
        s = last
        n = len(s)
        while n > 0:
            while n > 0 and s[n - 1] == 0x20:
                n -= 1
            if width_of(s[:n] + ELLIPSIS, font, scale) <= width or n == 0:
                break  # nothing left of the line: the ellipsis alone, as the engine
            n -= 1
            while n > 0 and (s[n] & 0xC0) == 0x80:
                n -= 1
        last = s[:n] + ELLIPSIS
    return {"scale": scale, "lines": [t.decode("utf-8", errors="replace") for t in texts], "shown": shown, "cut": cut,
            "scrolling": scrolling, "last": last.decode("utf-8", errors="replace")}
