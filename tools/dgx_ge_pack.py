"""
The packer of DGX-GE: sets of pictures, pictures and blobs into one pack the
engine reads where it lies (see include/dgx_ge_pack.h), and a C header that
names their numbers.

It knows nothing of any game. A game has a script of its own that finds its
pictures and calls this one:

    from dgx_ge_pack import Pack

    pack = Pack("MYGAME")
    hero = pack.add_set("HERO", colors)                 # up to 15 colors, RGB888
    pack.add_png("HERO_STAND", hero, "stand.png", origin="bottom-left")
    pack.add_blob("LEVEL_1", open("level1.bin", "rb").read())
    pack.write("mygame.pack", "mygame_pack.h")

A picture is unpacked, 1, 4 or 8 bits a pixel, rows from a new byte, the
leftmost pixel in the high bits. A pixel 0 is transparent. Values 1.. are
those of the colors of its set: a set has 15 colors for 4-bit pictures, up to
255 for 8-bit ones; a 1-bit picture has no colors, it is drawn in one color
the game gives.
"""

import json
import os
import re
import struct
import sys
import zlib

MAGIC = 0x4B504744  # "DGPK"
VERSION = 4
HEADER_BYTES = 52  # the last 4: the fingerprint of the pack (dgx_ge_pack_id)
TILES_MAGIC = 0x53544744  # "DGTS"
TILES_VERSION = 9


def _align4(data):
    return data + b"\0" * (-len(data) % 4)


def unpack_pixels(data, width, height, bits):
    """The pixel values of rows of bits (pack_pixels), row after row."""
    if bits == 8:
        return list(data[:width * height])
    stride, per_byte, out = (width * bits + 7) // 8, 8 // bits, []
    for y in range(height):
        for x in range(width):
            byte = data[y * stride + x // per_byte]
            out.append(byte >> (8 - bits * (x % per_byte + 1)) & ((1 << bits) - 1))
    return out


def pack_pixels(values, width, height, bits):
    """Pixel values, row after row, into rows of bits, every row from a new byte, the leftmost pixel in the high bits."""
    out = bytearray()
    for y in range(height):
        row = values[y * width:(y + 1) * width]
        if bits == 8:
            out += bytes(row)
            continue
        per_byte = 8 // bits
        for x in range(0, width, per_byte):
            byte = 0
            for i in range(per_byte):
                v = row[x + i] if x + i < width else 0
                byte |= v << (8 - bits * (i + 1))
            out.append(byte)
    return bytes(out)


def name_of(text):
    """A name of the pack and the header: letters and digits kept, the rest '_', upper case."""
    return re.sub(r"[^A-Za-z0-9]", "_", text).upper()


TIMER_RESETS = ("never", "scene", "map")


# How a sound's samples are kept (dgx_ge_sound.h): 8 bits unsigned, IMA ADPCM of 4 bits.
SOUND_CODECS = {"pcm8": 1, "adpcm": 2}
IMA_STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
             143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
             1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
             9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def ima_adpcm_stream(samples):
    """
    Signed 16-bit samples as IMA ADPCM: the state it starts from (the predictor at the first sample, the step index
    that follows its first 64 samples best: from the smallest step a loud start lags) and the nibbles.
    """
    def error(index):
        state = [samples[0], index]
        total = 0
        for v in samples[:64]:
            ima_code(state, v)
            total += (state[0] - v) ** 2
        return total

    index   = min(range(89), key=error)
    state   = [samples[0], index]
    nibbles = [ima_code(state, v) for v in samples]
    return (samples[0], index), bytes(nibbles[k] | (nibbles[k + 1] << 4 if k + 1 < len(nibbles) else 0) for k in range(0, len(nibbles), 2))


def ima_code(state, v):
    """The nibble of one sample against the state [predictor, step index] of its channel, the state moved on."""
    pred, index = state
    step, diff, code = IMA_STEPS[index], v - pred, 0
    if diff < 0:
        code, diff = 8, -diff
    delta = step >> 3
    if diff >= step:
        code |= 4; diff -= step; delta += step
    step >>= 1
    if diff >= step:
        code |= 2; diff -= step; delta += step
    step >>= 1
    if diff >= step:
        code |= 1; delta += step
    state[0] = max(-32768, min(32767, pred - delta if code & 8 else pred + delta))
    state[1] = max(0, min(88, index + IMA_INDEX[code & 7]))
    return code


def read_wav(path, stereo=False):
    """A WAV file of PCM, 8 or 16 bits: its samples, signed 16-bit (stereo: (left, right) pairs, else mono), and its rate."""
    import wave
    with wave.open(path, "rb") as w:
        channels, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.readframes(w.getnframes())
    if width not in (1, 2):
        raise ValueError(f"{path}: 8 or 16 bits a sample, not {8 * width}")
    if width == 1:
        values = [(b - 128) << 8 for b in frames]
    else:
        values = list(struct.unpack(f"<{len(frames) // 2}h", frames))
    if stereo and channels == 2:
        return [(values[i], values[i + 1]) for i in range(0, len(values), 2)], rate
    return [sum(values[i:i + channels]) // channels for i in range(0, len(values), channels)], rate


class Pack:
    def __init__(self, prefix):
        self.prefix = prefix
        self.sets = []    # (name, [(r, g, b), ...])
        self.images = []  # (name, set, width, height, origin_x, origin_y, bits, pixels)
        self.blobs = []   # (name, bytes)
        self.masks = []   # (image, index, x, y, width, height, bits): masks of collisions, from the origin of their picture
        self.mask_names = {}  # index -> name, for the header
        self.points = {}  # (image, name number) -> (x, y, z): points of pictures, from their origin
        self.point_names = {}  # name -> number, as they come, for the header
        self.anims = []   # (name, scripts, events, params) of blobs of animation
        self.games = {}   # name of an animation -> the questions to the game it declares (game.<name>), in order
        self.events = []  # the events of the pack, by name: one number for every kind
        self.tilesets = []  # (name, tiles, slots) of blobs of tilesets, for the header
        self.graphs = []    # graphs of scenes (dgx_ge_graph.py), for the header
        self.game = []      # the constants of the game: (name, value), in the order written
        self.timers = []    # the names of the timers of the pack: their numbers (timer(NAME), add_timers)
        self.sounds = []    # the names of the sounds, by their numbers (dgx_ge_sound.h)
        self.sound_data = []  # their samples, rate, loop, codec (None: a name only), by the same numbers
        self.timer_defs = {}  # name -> (kind, period ms, limit ms, reset, event name or None, starts with the game)
        self.chases = []    # (kind, its record) of the blob CHASE: how kinds chase the hero (add_chase)
        self.meets = None  # {"own": rows, "foreign": rows}, 16 rows of 16 bits: which masks meet
        from dgx_ge_dialog import Strings
        self.strings = Strings()  # the strings of dialogs, in the language of the pack (translate)
        self.styles = []   # the styles dialogs name, by name: their numbers
        self.style_data = {}  # name -> its fields (add_style); a style named and not made is the engine's default
        self.fonts = []    # the game's fonts styles name, by name: their numbers (the game gives them in this order)
        self.language = None  # the language of the strings: po/<language>.po next to the editor's file (add_sprites)
        self.dialogs = []  # (name, roles) of blobs of dialogs, for the header
        self.warnings = []

    def add_set(self, name, colors):
        """A set of pictures and its colors, those of the values 1..; returns its number."""
        if len(colors) > 255:
            raise ValueError(f"set {name}: {len(colors)} colors, 255 at most")
        if any(name_of(n) == name_of(name) for n, _ in self.sets):
            raise ValueError(f"set {name}: a set of that name already (the header names them alike)")
        self.sets.append((name, [tuple(c[:3]) for c in colors]))
        return len(self.sets) - 1

    def add_image(self, name, set_id, width, height, values, bits, origin=(0, 0)):
        """A picture of pixel values (0 transparent), row after row; returns its number."""
        if bits not in (1, 4, 8):
            raise ValueError(f"picture {name}: {bits} bits a pixel")
        top = max(values, default=0)
        if top >= 1 << bits:
            raise ValueError(f"picture {name}: value {top} does not fit {bits} bits")
        if bits > 1 and top > len(self.sets[set_id][1]):
            raise ValueError(f"picture {name}: value {top}, set {self.sets[set_id][0]} has {len(self.sets[set_id][1])} colors")
        self.images.append((name, set_id, width, height, origin[0], origin[1], bits, pack_pixels(values, width, height, bits)))
        return len(self.images) - 1

    def add_png(self, name, set_id, path, origin="bottom-left", bits=None, by_index=False, empty_rgb=None):
        """
        A picture from a PNG of a palette, its color 0 transparent. Its colors are
        found among those of the set by their RGB; a color the set has not gets
        the nearest one, with a warning. A picture of two colors is 1-bit.

        by_index: the indices of the PNG are the values as they are, its colors
        are not looked at (a picture whose colors come from another set).
        empty_rgb: a color that means nothing is there where the set has it not,
        and becomes transparent.
        """
        from PIL import Image

        im = Image.open(path)
        if im.mode != "P":
            raise ValueError(f"{path}: not a picture of a palette ({im.mode})")
        palette = im.getpalette()
        width, height = im.size
        raw = list(im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata())
        if bits is None:
            bits = 1 if len(palette) // 3 <= 2 else 4 if len(self.sets[set_id][1]) <= 15 else 8
        if bits == 1:
            values = [1 if v else 0 for v in raw]
        elif by_index:
            values = raw
        else:
            colors = self.sets[set_id][1]
            lookup = {}
            for v in sorted(set(raw)):
                if v == 0:
                    lookup[v] = 0
                    continue
                rgb = tuple(palette[3 * v:3 * v + 3])
                if rgb in colors:
                    lookup[v] = colors.index(rgb) + 1
                elif empty_rgb is not None and rgb == tuple(empty_rgb):
                    lookup[v] = 0
                else:
                    best = min(range(len(colors)), key=lambda i: sum((a - b) ** 2 for a, b in zip(colors[i], rgb)))
                    lookup[v] = best + 1
                    self.warnings.append(f"{path}: color {v} {rgb} is not in set {self.sets[set_id][0]}, taken as {colors[best]}")
            values = [lookup[v] for v in raw]
        if origin == "bottom-left":
            origin = (0, height - 1)
        elif origin == "top-left":
            origin = (0, 0)
        return self.add_image(name, set_id, width, height, values, bits, origin)

    def add_mask(self, image, index, x, y, width, height, values):
        """
        A mask of collisions of a picture: index 0..15, its rectangle from the
        origin of the picture, values 0/1 row after row. A picture has a mask
        of an index once.
        """
        if not 0 <= index <= 15:
            raise ValueError(f"mask {index} of picture {image}: indices are 0..15")
        if any(m[0] == image and m[1] == index for m in self.masks):
            raise ValueError(f"picture {image} has mask {index} twice")
        if len(values) != width * height:
            raise ValueError(f"mask {index} of picture {image}: {len(values)} values for {width}x{height}")
        self.masks.append((image, index, x, y, width, height, pack_pixels([1 if v else 0 for v in values], width, height, 1)))

    def add_point(self, image, name, x, y, z=1):
        """
        A point of a picture: a place a child is held at (a hand), from its
        origin, and the side of the body it is drawn on (z > 0 before, < 0
        behind, as a child's z). The name gets a number for the header:
        <PREFIX>_POINT_<NAME>. Given again the same, nothing; otherwise an error.
        """
        if not -128 <= z <= 127 or z == 0:
            raise ValueError(f"point {name} of picture {image}: z {z}, -128..127 and not 0")
        number = self.point_names.setdefault(name_of(name), len(self.point_names))
        if number > 255:
            raise ValueError(f"point {name}: 256 names of points at most")
        was = self.points.get((image, number))
        if was is not None and was != (x, y, z):
            raise ValueError(f"picture {image} has point {name} at {was} and at {(x, y, z)}")
        self.points[(image, number)] = (x, y, z)

    def add_sprites(self, path, images=None):
        """
        The sprites of the sprite editor (dgx-ge-sprites, its
        sprite-format-ru.md): every set a set of colors (those of its PNGs),
        every frame a picture <SET>_<FILE> (or its packName, as the editor
        gives it: DAGGER) with its origin and masks: those
        drawn, or else the default mask of the set made of the points of the
        picture. The names of the masks go into the header; "meets", text, is
        which of them meet (add_meets). A set with a file
        <set>.anim next to the JSON gets its animation too (all of them made together, add_anims: they sense and
        bear one another whatever the order of the sets); its sets of tiles
        become blobs of tiles (add_tileset). images: where the
        PNGs are, by default next to the JSON (in <set>/ or right there).
        Returns {set name: set number}.
        """
        from PIL import Image

        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        if doc.get("format") != "dgx-ge-sprites":
            raise ValueError(f"{path}: not a file of the sprite editor")
        if doc.get("formatVersion", 0) > 1:
            raise ValueError(f"{path}: format version {doc.get('formatVersion')} is unknown")
        here = os.path.dirname(os.path.abspath(path))
        images = images or here
        known = set()
        for m in doc.get("masks", []):
            self.name_mask(m["index"], name_of(m.get("name") or f"MASK_{m['index']}"))
            known.add(m["index"])
        if doc.get("meets") is not None:
            self.add_meets(doc["meets"])
        made = {}
        pack_names = {}  # (set, file) -> the name of the picture in the pack: <SET>_<FILE>, or the frame's packName

        def pack_name(set_name, fr):
            if fr.get("packName"):
                return name_of(fr["packName"])
            return name_of(f"{set_name}_{os.path.splitext(fr['image'])[0]}")

        for st in doc.get("sets", []):
            for fr in st.get("frames", []):
                pack_names[(st["name"], fr["image"])] = pack_name(st["name"], fr)
        anims = []
        for st in doc.get("sets", []):
            frames = st.get("frames", [])
            files = []
            for fr in frames:
                for p in (os.path.join(images, st["name"], fr["image"]), os.path.join(images, fr["image"])):
                    if os.path.exists(p):
                        files.append(p)
                        break
                else:
                    raise ValueError(f"{path}: no picture {fr['image']} of set {st['name']}")
            # the colors of the set: the palette of its pictures, up to the highest index they use
            pictures = [Image.open(p) for p in files]
            top = max((max(im.getdata(), default=0) for im in pictures), default=0)
            palette = pictures[0].getpalette() if pictures else []
            colors = [tuple(palette[3 * i:3 * i + 3]) for i in range(1, top + 1)]
            set_id = self.add_set(name_of(st["name"]), colors)
            made[st["name"]] = set_id
            bits = 4 if top <= 15 else 8
            for fr, p, im in zip(frames, files, pictures):
                if (fr["width"], fr["height"]) != im.size:
                    raise ValueError(f"{path}: {fr['image']} is {im.size}, the marks are of {fr['width']}x{fr['height']}")
                ox, oy = fr["origin"]["x"], fr["origin"]["y"]
                image = self.add_png(pack_names[(st["name"], fr["image"])], set_id, p, origin=(ox, oy), bits=bits,
                                     by_index=True)
                masks = fr.get("masks") or {}
                for key, m in masks.items():
                    index = int(key)
                    if index not in known:
                        raise ValueError(f"{path}: {fr['image']} has mask {index}, not described in the file")
                    if not m["width"] or not m["height"]:
                        continue  # drawn empty: this zone meets nothing in this frame
                    values = [(int(m["rows"][r][c // 4], 16) >> (3 - c % 4)) & 1 for r in range(m["height"]) for c in range(m["width"])]
                    self.add_mask(image, index, m["x"], m["y"], m["width"], m["height"], values)
                for pname, pt in (fr.get("points") or {}).items():
                    self.add_point(image, pname, pt["x"], pt["y"], pt.get("z", 1))
                if not masks and st.get("defaultMask") is not None:
                    w, h = im.size
                    self.add_mask(image, st["defaultMask"], -ox, -oy, w, h, [1 if v else 0 for v in im.getdata()])
            anim = os.path.join(here, f"{st['name']}.anim")
            if os.path.exists(anim):
                with open(anim, encoding="utf-8") as f:
                    anims.append((name_of(st["name"]), f.read()))
        # the animations of the sets together, in their order: each knows the others (sense, appear) and every picture
        if anims:
            self.add_anims(anims)
        # palettes: sets of colors of no pictures (the 16 of the EGA, the colors of guards), named as sets: a piece's color
        # [name, n] may be of one, and the header names it (<PREFIX>_SET_<NAME>)
        for pal in doc.get("palettes", []):
            if any(name_of(pal["name"]) == name_of(n) for n in made):
                raise ValueError(f"{path}: palette {pal['name']}: a set of that name already (the header names them alike)")
            if not 1 <= len(pal["colors"]) <= 255:
                raise ValueError(f"{path}: palette {pal['name']}: 1 .. 255 colors")
            made[pal["name"]] = self.add_set(name_of(pal["name"]), [tuple(c) for c in pal["colors"]])
        # sets of tiles: pieces are pictures of the sets above; the names of their layers become slots, in order
        pictures = {im[0]: i for i, im in enumerate(self.images)}
        for ts in doc.get("tilesets", []):
            slots, tiles = [], []
            for t in ts.get("tiles", []):
                pieces = []

                def picture_of(ref):
                    picture = pack_names.get((ref["set"], ref["image"])) or name_of(f"{ref['set']}_{os.path.splitext(ref['image'])[0]}")
                    if picture not in pictures:
                        raise ValueError(f"{path}: tile {t['name']} of {ts['name']}: no picture {ref['set']}/{ref['image']}")
                    return pictures[picture]

                for pc in t.get("pieces", []):
                    if pc["layer"] not in slots:
                        slots.append(pc["layer"])
                    piece = {"dx": pc.get("dx", 0), "dy": pc.get("dy", 0), "slot": slots.index(pc["layer"]), "key": pc.get("key", 0)}
                    if pc.get("by_mod"):
                        piece["by_mod"] = [None if r is None else picture_of(r) for r in pc["by_mod"]]
                    elif pc.get("pick") is not None:
                        piece["pick"], piece["pictures"] = pc["pick"], [None if r is None else picture_of(r) for r in pc["pictures"]]
                    elif pc.get("draw") == "fill":
                        piece["size"] = pc["size"]
                    elif pc.get("cycle"):
                        piece["cycle"] = [(picture_of(r), r["ms"]) for r in pc["cycle"]]
                    else:
                        piece["image"] = picture_of(pc)
                    if pc.get("around"):
                        piece["around"] = pc["around"]
                    if pc.get("variant") is not None:
                        piece["variant"] = pc["variant"]
                    for field in ("with", "step", "draw", "when"):
                        if pc.get(field) is not None:
                            piece[field] = pc[field]
                    color = pc.get("color")
                    if isinstance(color, list):
                        # a color of another set, by its name in the file
                        if color[0] not in made:
                            raise ValueError(f"{path}: tile {t['name']} of {ts['name']}: no set {color[0]} for its color")
                        piece["color"] = (made[color[0]], color[1])
                    elif color is not None:
                        piece["color"] = color
                    pieces.append(piece)
                tile = {"name": t["name"], "kind": t.get("kind", 0), "solid": t.get("solid", {}), "pieces": pieces, "life": t.get("life"),
                        "seen_as": t.get("seen_as")}
                if t.get("closes") is not None:
                    tile["closes"] = t["closes"]
                if t.get("veil"):
                    tile["veil"] = t["veil"]
                if t.get("bodies"):
                    tile["bodies"] = [{k: b[k] for k in ("dx", "dy", "w", "h", "when", "for") if b.get(k) is not None} for b in t["bodies"]]
                if t.get("opening"):
                    tile["opening"] = {k: t["opening"][k] for k in ("dx", "dy", "w", "h", "when") if t["opening"].get(k) is not None}
                tiles.append(tile)
            self.add_tileset(name_of(ts["name"]), ts["cellWidth"], ts["cellHeight"], tiles, slots,
                             {n: tuple(f) for n, f in ts.get("fields", {}).items()})
        # styles of dialogs: how they look (add_style), the editor's keys
        for st in doc.get("styles", []):
            f, colors = {}, st.get("colors", {})
            for key, to in (("frame", "frame"), ("insets", "insets"), ("place", "place"), ("padding", "padding"), ("font", "font"),
                            ("scale", "scale"), ("important", "important"), ("scrollMs", "scroll_ms"), ("waitMs", "wait_ms"),
                            ("borderWidth", "border_width"), ("appear", "appear"), ("letterMs", "letter_ms"), ("showName", "show_name"),
                            ("portrait", "portrait"), ("portraitWidth", "portrait_width"), ("pen", "pen"), ("align", "align")):
                if st.get(key) is not None:
                    f[to] = st[key]
            if st.get("picture"):
                ref = st["picture"]
                f["picture"] = pack_names.get((ref["set"], ref["image"])) or name_of(f"{ref['set']}_{os.path.splitext(ref['image'])[0]}")
            for c in ("fill", "border", "text", "bar", "name"):
                if colors.get(c) is not None:
                    f["name_color" if c == "name" else c] = tuple(colors[c])
            self.add_style(st["name"], **f)
        # dialogs: dialogs/*.dlg next to the file, in the language given (po/<language>.po), if any
        if self.language and os.path.exists(os.path.join(here, "po", f"{self.language}.po")):
            self.translate(os.path.join(here, "po", f"{self.language}.po"))
        folder = os.path.join(here, "dialogs")
        for name in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
            if name.endswith(".dlg"):
                with open(os.path.join(folder, name), encoding="utf-8") as f:
                    self.add_dialog(f.read())
        return made

    def add_tileset(self, name, cell_width, cell_height, tiles, slots, fields=None):
        """
        A set of tiles as a blob ("DGTS", see include/dgx_ge_tiles.h). tiles: a list
        of {name, kind, solid: {top, left, right, bottom}, closes: {top, left, right, bottom}, bodies, pieces, life, seen_as}: life,
        the name of an animation added before: what a cell of the tile does by itself (dgx_ge_cells.h); seen_as, the name of
        another tile its neighbours see it as (a button held down, a floor to them): where a condition names that one, it
        names this one too;
        solid what stops, closes what ends the open space an actor is seen in (as solid unless given); veil: how much a
        look through it costs (sense ... seen: 0 none, 1..6 a point through it costs 2..7, 7 nothing seen); bodies: what of the
        cell is solid, when given (solid then only for the conditions of pieces and for sight): a list of {dx, dy, w, h,
        when, for}, a box from the bottom-left point of the cell moved by dx, dy (dy up is negative), w right and h up of it;
        for: the masks it holds (numbers 0..15; none: every one), a body of the rules of some (a person's) beside one
        as drawn for the rest; opening: a box as a body is ({dx, dy, w, h, when}), what an actor gone into it (up the
        stairs behind a door, DGX_GE_ITEM_INSIDE) is seen through;
        numbers or expressions of mod, variant and fields, as dx, dy of a piece; a piece stands
        by its origin at the bottom-left point of the cell moved by dx, dy, in a
        slot: a layer the scene says which of its own. A piece is a tuple
        (picture, dx, dy, slot, key) or a dict of:

          image                  a picture; or
          by_mod: [pictures]     the picture by the modifier of the cell (the last for any above); None: no piece then; or
          pick: expr, pictures: [pictures]  the picture by an expression, as by_mod; or
          cycle: [(picture, ms)] pictures going round by the time of the scene alone, every cell in step
          dx, dy                 numbers, or expressions of mod, variant and fields: "-mod / 4"
          slot, key
          around: {side: what}   drawn only where its neighbours are so: side up, down, left, right,
                                 up_left, up_right, down_left, down_right; what "empty", a tile, [tiles],
                                 "solid_top" (left, right, bottom, any), and "not " before any of them
          variant: n             drawn only in cells of this variant of decoration
          when: expr             drawn only where it is not 0: "mod > 0 and variant == 0"
          with: side             laid when the grid lays the cell on that side (up, right...), not its own;
                                 its place, its rules are still those of its own cell
          step: n                its turn among the pieces laid with a cell, 0 first (0..255)
          draw: "card" | "silhouette" | "fill"  on a rectangle of its color hiding what is behind, its shape in one
                                 color, or a rectangle of its color alone (size: [w, h], its bottom-left at dx, dy; no picture)
          color: n | (set, n)    of a card, a silhouette or a fill: 0 the paper of the frame, n the color n of its own
                                 set or of another; n may be an expression

        fields: names for bits of the modifier and the variant of a cell, for expressions:
        {"middle_x": ("variant", 1, 3)} is bits 1..3 of the variant; mod and variant are there anyway.

        slots: the names of the slots, in order. Tile n of the grid is
        tiles[n - 1]; 0 is empty. The header gets <PREFIX>_<NAME>_TILE_<tile>
        (numbers from 1) and <PREFIX>_<NAME>_SLOT_<slot>.
        """
        from dgx_ge_anim import Expr

        tile_number = {t["name"]: i + 1 for i, t in enumerate(tiles)}
        sides = {"up": 0, "down": 1, "left": 2, "right": 3, "up_left": 4, "up_right": 5, "down_left": 6, "down_right": 7}
        solids = {"solid_top": 1, "solid_left": 2, "solid_right": 4, "solid_bottom": 8, "solid_any": 15}
        table, pieces, pictures, conds, tile_list, exprs = bytearray(), bytearray(), bytearray(), bytearray(), bytearray(), bytearray()
        bodies = bytearray()
        distinct = {}  # the pictures of the pack, each once, by the order they come in
        laid = bytearray()
        count = 0

        words = {"mod": 0, "variant": 1}
        names = {"mod": (0, 0, 8), "variant": (1, 0, 32)}
        from dgx_ge_anim import FUNCS, VELOCITY_NAMES, WORDS

        for n, (word, first, bits) in (fields or {}).items():
            if n in names or n in FUNCS or n in WORDS or n in VELOCITY_NAMES or n in ("t", "now") or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", n):
                raise ValueError(f"tileset {name}: {n} cannot name a field")
            if word not in words or not 0 <= first < 32 or not 1 <= bits <= 32 - first:
                raise ValueError(f"tileset {name}: field {n}: bits {first}..{first + bits - 1} of {word}?")
            names[n] = (words[word], first, bits)

        def offset_of(value, where):
            """A number of a piece, or an expression of mod, variant and fields: (number, the place of the expression or 0xFFFF)."""
            if isinstance(value, (int, float)):
                return int(value), 0xFFFF
            e = Expr(str(value), {}, {}, False, where, names, actor=False)
            if e.constant() is not None:
                from dgx_ge_anim import half_away
                return half_away(e.constant()), 0xFFFF
            at = len(exprs)
            exprs.extend(e.code())
            return 0, at

        def condition(side, what, where):
            nonlocal tile_list
            negate = 0
            if isinstance(what, str) and what.startswith("not "):
                negate, what = 0x80, what[4:].strip()
            if what == "empty":
                return struct.pack("<BBHH", sides[side], 0 | negate, 0, 0)
            if isinstance(what, str) and what in solids:
                return struct.pack("<BBHH", sides[side], 2 | negate, solids[what], 0)
            names = what if isinstance(what, list) else [what]
            names = names + [t["name"] for t in tiles if t.get("seen_as") in names and t["name"] not in names]
            first = len(tile_list) // 2
            for n in names:
                if n not in tile_number:
                    raise ValueError(f"{where}: around {side}: no tile {n}")
                tile_list += struct.pack("<H", tile_number[n])
            return struct.pack("<BBHH", sides[side], 1 | negate, first, len(names))

        for t in tiles:
            solid = t.get("solid", {})
            bits = (1 if solid.get("top") else 0) | (2 if solid.get("left") else 0) | (4 if solid.get("right") else 0) | (8 if solid.get("bottom") else 0)
            if len(t["pieces"]) > 255:
                raise ValueError(f"tileset {name}: tile {t['name']} has {len(t['pieces'])} pieces, 255 at most")
            closes = t.get("closes", solid)  # for sight; as it is solid unless said
            seen = (1 if closes.get("top") else 0) | (2 if closes.get("left") else 0) | (4 if closes.get("right") else 0) | (8 if closes.get("bottom") else 0)
            if t.get("opening"):
                seen |= 16  # the record after its bodies is its opening
            veil = t.get("veil", 0)
            if not isinstance(veil, int) or not 0 <= veil <= 7:
                raise ValueError(f"tileset {name}: tile {t['name']}: veil 0..7, not {veil}")
            seen |= veil << 5  # how much it veils a look through it (sense ... seen)
            if len(t.get("bodies", [])) > 255:
                raise ValueError(f"tileset {name}: tile {t['name']} has more than 255 bodies")
            life = 0xFFFF
            if t.get("life"):
                found = [i for i, (n, _) in enumerate(self.blobs) if n == name_of(t["life"])]
                if not found:
                    raise ValueError(f"tile {t['name']}: no animation {t['life']} for its life (add it before the tiles)")
                life = found[0]
            table += struct.pack("<HBBHBBHH", t.get("kind", 0), bits, len(t["pieces"]), count, seen, len(t.get("bodies", [])), len(bodies) // 20,
                                 life)
            for b in t.get("bodies", []) + ([t["opening"]] if t.get("opening") else []):
                where = f"tileset {name}: tile {t['name']}: " + ("its opening" if b is t.get("opening") else "a body")
                if b is t.get("opening") and b.get("for"):
                    raise ValueError(f"{where}: holds no masks")
                values = [offset_of(b.get(k, 0), where) for k in ("dx", "dy", "w", "h")]
                when_e = 0xFFFF
                if b.get("when") is not None:
                    always, when_e = offset_of(str(b["when"]), where)
                    if when_e == 0xFFFF and not always:
                        raise ValueError(f"{where}: when {b['when']} is never")
                held = 0
                for m in b.get("for", []) or []:
                    if not 0 <= int(m) <= 15:
                        raise ValueError(f"{where}: holds mask {m}, 0..15")
                    held |= 1 << int(m)
                bodies += struct.pack("<hhhhHHHHHH", *[v for v, _ in values], *[e for _, e in values], when_e, held)
            steps_of = [0] * 9  # by the side it is laid with: a bit a step
            for pc in t["pieces"]:
                side = 0 if isinstance(pc, tuple) or pc.get("with", "self") == "self" else sides[pc["with"]] + 1
                step = 0 if isinstance(pc, tuple) else pc.get("step", 0)
                if step < 32:
                    steps_of[side] |= 1 << step
            laid += struct.pack("<9I", *steps_of)
            for pc in t["pieces"]:
                if isinstance(pc, tuple):
                    pc = {"image": pc[0], "dx": pc[1], "dy": pc[2], "slot": pc[3], "key": pc[4]}
                where = f"tileset {name}: tile {t['name']}"
                if not 0 <= pc.get("slot", 0) < len(slots):
                    raise ValueError(f"{where}: no slot {pc.get('slot')}")
                first = len(pictures) // 4
                pick_e = 0xFFFF
                if "by_mod" in pc:
                    mode, items = 1, [(p, 0) for p in pc["by_mod"]]
                elif "pick" in pc:
                    mode, items = 1, [(p, 0) for p in pc["pictures"]]
                    _, pick_e = offset_of(pc["pick"] if isinstance(pc["pick"], str) else str(pc["pick"]), where)
                    if pick_e == 0xFFFF:
                        raise ValueError(f"{where}: pick {pc['pick']} is a constant: give the image")
                elif pc.get("draw") == "fill":
                    mode, items = 0, [(None, 0)]
                elif "cycle" in pc:
                    mode, items = 2, [(p, ms) for p, ms in pc["cycle"]]
                else:
                    mode, items = 0, [(pc["image"], 0)]
                if mode == 1:
                    # the last stands for any modifier above: what repeats at the end is said once
                    while len(items) > 1 and items[-1] == items[-2]:
                        items.pop()
                if not items or len(items) > 255:
                    raise ValueError(f"{where}: a piece of {len(items)} pictures")
                for p, ms in items:
                    if p is not None and p not in distinct:
                        distinct[p] = len(distinct)
                    pictures += struct.pack("<HH", 0xFFFF if p is None else distinct[p], ms)
                dx, dx_e = offset_of(pc.get("dx", 0), where)
                dy, dy_e = offset_of(pc.get("dy", 0), where)
                cond_first = len(conds) // 6
                for side, what in pc.get("around", {}).items():
                    if side not in sides:
                        raise ValueError(f"{where}: no side {side}")
                    conds += condition(side, what, where)
                variant = pc.get("variant")
                with_side = 0 if pc.get("with", "self") == "self" else sides[pc["with"]] + 1
                draw = {"normal": 0, "card": 1, "silhouette": 2, "fill": 3}[pc.get("draw", "normal")]
                color = pc.get("color", 0)
                color_set, color = (0xFF, color) if isinstance(color, (int, str)) else color
                color_e = 0xFFFF
                if isinstance(color, str):
                    color, color_e = offset_of(color, where)
                when_e = 0xFFFF
                if pc.get("when") is not None:
                    always, when_e = offset_of(str(pc["when"]), where)
                    if when_e == 0xFFFF and always:
                        pass  # always drawn: no test
                    elif when_e == 0xFFFF:
                        raise ValueError(f"{where}: when {pc['when']} is never")
                fill_w, fill_h = pc.get("size", (0, 0)) if draw == 3 else (0, 0)
                if draw == 3 and not (0 < fill_w < 256 and 0 < fill_h < 256):
                    raise ValueError(f"{where}: a fill of size {fill_w} x {fill_h}")
                pieces += struct.pack("<HBBhhHHBBhHBBBBBBHHBBH", first, len(items), mode, dx, dy, dx_e, dy_e, pc.get("slot", 0),
                                      0xFF if variant is None else variant, pc.get("key", 0), cond_first, len(conds) // 6 - cond_first,
                                      with_side, pc.get("step", 0), draw, color_set, color, pick_e, color_e, fill_w, fill_h, when_e)
                count += 1
        header = struct.pack("<IHHHHHHHHHHHH", TILES_MAGIC, TILES_VERSION, cell_width, cell_height, len(tiles), len(slots), count,
                             len(conds) // 6, len(pictures) // 4, len(tile_list) // 2, len(exprs), len(distinct), len(bodies) // 20)
        images = b"".join(struct.pack("<H", p) for p in distinct)
        blob = header + bytes(table) + bytes(laid) + bytes(pieces) + images + bytes(pictures) + bytes(conds) + bytes(tile_list) + bytes(exprs) + bytes(bodies)
        self.tilesets.append((name, [t["name"] for t in tiles], list(slots)))
        return self.add_blob(name, blob)

    def add_links(self, name, links):
        """
        The links of a map (see include/dgx_ge_links.h) as a blob: a list of (cell, event, last, value): the cell its
        life is told (the game's number; None: nobody, a place kept), the event (a name of the pack's events; None: the
        one the source fires), whether it is the last of its source, the value given to own param 0 of the target's life
        first (None: none). A source names its first link.
        """
        out = struct.pack("<IHH", 0x4B4C4744, 1, len(links))
        for cell, event, last, value in links:
            if event is not None and event not in self.events:
                self.events.append(event)
            ev = 0xFF if event is None else self.events.index(event)
            if event is not None and ev > 254:
                raise ValueError(f"links {name}: 255 events at most")
            out += struct.pack("<HBBh", 0xFFFF if cell is None else cell, ev, 1 if last else 0, -32768 if value is None else value)
        return self.add_blob(name, out)

    def add_graph(self, graph):
        """
        A graph of scenes (dgx_ge_graph.py: Graph) as a blob named as the graph; the rules of its maps as animations
        (<GRAPH>_<MAP>_RULES), the blobs of their data added before. The header gets <PREFIX>_<GRAPH>_EXIT_<name>,
        _ENTRY_<name>, _MAP_<name>.
        """
        for name, data, rules, _ in graph.maps:
            if rules:
                self.add_anim(name_of(f"{graph.name}_{name}_RULES"), graph.rules_text(rules))
        names = {n: i for i, (n, _) in enumerate(self.blobs)}

        def blob_of(name):
            if name is None:
                return 0xFFFF
            if name_of(name) not in names:
                raise ValueError(f"graph {graph.name}: no blob {name}")
            return names[name_of(name)]

        def event_of(event):
            if event not in self.events:
                self.events.append(event)
            return self.events.index(event)

        blob = graph.blob(blob_of, event_of)
        self.graphs.append(graph)
        return self.add_blob(name_of(graph.name), blob)

    def add_game(self, **constants):
        """
        Constants of the game (include/dgx_ge_game.h): numbers the engine's parts take as they are (kinds, lives,
        records, boxes, ...) and the game's own; written in this order, the header names their places
        (<PREFIX>_GAME_<NAME>). Given again, a value is replaced, its place kept.
        """
        for name, value in constants.items():
            if not -2 ** 31 <= int(value) < 2 ** 31:
                raise ValueError(f"game {name}: {value} is no 32-bit number")
            for i, (n, _) in enumerate(self.game):
                if n == name:
                    self.game[i] = (name, int(value))
                    break
            else:
                self.game.append((name, int(value)))

    def add_timer(self, name, once=None, every=None, until=None, reset="never", event=None, start=None):
        """
        A timer of the game (include/dgx_ge_timers.h), by its name (timer(NAME) in expressions reads its milliseconds):
          once="5s"                 goes off once, so long after its start;
          every="1s"                every so often, until it is stopped;
          every="500ms", until="10s"  every so often, no longer than so long from its start;
          reset "never" | "scene" | "map": stopped as one leaves the scene, the map it was started in, or never;
          event: a name of the pack's events, sent to its owner as it goes off (None: none, a clock);
          start "game": going from the start of the game, its owner the game (None: started by the game or a script).
        Times: 500ms, 1s, 60s; the header gets <PREFIX>_TIMER_<NAME>.
        """
        def ms(text, what):
            m = re.fullmatch(r"(\d+)\s*(ms|s)", str(text).strip())
            if not m or not 1 <= int(m.group(1)) * (1000 if m.group(2) == "s" else 1) < 2 ** 32:
                raise ValueError(f"timer {name}: {what} is a time, 500ms or 1s, not {text}")
            return int(m.group(1)) * (1000 if m.group(2) == "s" else 1)

        if (once is None) == (every is None) or (until is not None and every is None):
            raise ValueError(f"timer {name}: once=<time>, every=<time> or every=<time>, until=<time>")
        if reset not in TIMER_RESETS or start not in (None, "game"):
            raise ValueError(f"timer {name}: reset is never, scene or map; start is game or nothing")
        if name in self.timer_defs:
            raise ValueError(f"timer {name} twice")
        kind = 0 if once is not None else 2 if until is not None else 1
        period = ms(once if once is not None else every, "its period")
        limit = ms(until, "its limit") if until is not None else 0
        if kind == 2 and limit < period:
            raise ValueError(f"timer {name}: until {until} is shorter than its period: it would never go off")
        if event is not None and event not in self.events:
            self.events.append(event)
        if name not in self.timers:
            self.timers.append(name)
        if len(self.timers) > 255:
            raise ValueError("255 timers at most")
        self.timer_defs[name] = (kind, period, limit, TIMER_RESETS.index(reset), event, start == "game")

    def _timers_blob(self):
        missing = [n for n in self.timers if n not in self.timer_defs]
        if missing:
            raise ValueError(f"timer {missing[0]} is read (timer({missing[0]})) but never described: add_timer")
        out = struct.pack("<IHH", 0x4D544744, 1, len(self.timers))  # "DGTM"
        for n in self.timers:
            kind, period, limit, reset, event, at_start = self.timer_defs[n]
            ev = 0xFF if event is None else self.events.index(event)
            if event is not None and ev > 254:
                raise ValueError(f"timer {n}: 255 events at most")
            out += struct.pack("<BBBBII", kind, reset, ev, 1 if at_start else 0, period, limit)
        return out

    def add_show(self, name, pictures=(), actors=(), cues=()):
        """
        A show (include/dgx_ge_show.h): a scene that plays by itself, its blob named so.
          pictures: its backdrop, [{"picture": a picture of the pack, "x", "y" (its origin), "layer", "flip", "color"}];
          actors: [{"name": its role in the cues, "kind": an animation added before, "script": a script of it (its first:
            none), "x", "y", "flip", "layer", "color" (of pictures of 1 bit)}];
          cues: [(time, role, event)]: at so long from the start ("500ms", "2s", or ms), the actor of that role told an event
            (a name of the pack's events); the role "end": the show is over then. In any order: they are put in time.
        """
        def ms(t):
            if isinstance(t, (int, float)):
                return int(t)
            m = re.fullmatch(r"(\d+)\s*(ms|s)", str(t).strip())
            if not m:
                raise ValueError(f"show {name}: a time, 500ms or 2s, not {t}")
            return int(m.group(1)) * (1000 if m.group(2) == "s" else 1)

        images = {im[0]: i for i, im in enumerate(self.images)}
        blobs = {n: i for i, (n, _) in enumerate(self.blobs)}
        anims = {a[0]: a for a in self.anims}
        out = bytearray()
        for p in pictures:
            if p["picture"] not in images:
                raise ValueError(f"show {name}: no picture {p['picture']}")
            out += struct.pack("<HhhBBBx", images[p["picture"]], p.get("x", 0), p.get("y", 0), p.get("layer", 0), 1 if p.get("flip") else 0,
                               p.get("color", 0))
        roles = []
        for a in actors:
            if a["kind"] not in anims:
                raise ValueError(f"show {name}: no animation {a['kind']} (add it before the show)")
            scripts = anims[a["kind"]][1]
            script = a.get("script", scripts[0])
            if script not in scripts:
                raise ValueError(f"show {name}: no script {script} of {a['kind']}")
            if a["name"] in roles or a["name"] == "end":
                raise ValueError(f"show {name}: a role {a['name']} twice, or named end")
            roles.append(a["name"])
            out += struct.pack("<HhhBBBB", blobs[a["kind"]], a.get("x", 0), a.get("y", 0), scripts.index(script), a.get("layer", 0),
                               1 if a.get("flip") else 0, a.get("color", 0))
        timed = []
        for at, role, event in cues:
            if role == "end":
                timed.append((ms(at), 0xFF, 0))
                continue
            if role not in roles:
                raise ValueError(f"show {name}: a cue for no role {role}")
            if event not in self.events:
                raise ValueError(f"show {name}: no event {event} of the pack")
            timed.append((ms(at), roles.index(role), self.events.index(event)))
        if not any(r == 0xFF for _, r, _ in timed):
            raise ValueError(f"show {name}: no end")
        timed.sort(key=lambda c: (c[0], c[1] == 0xFF))  # at one moment the end comes last
        out += b"".join(struct.pack("<IBBxx", *c) for c in timed)
        return self.add_blob(name, struct.pack("<IHHHH", 0x48534744, 1, len(pictures), len(actors), len(timed)) + bytes(out))

    def add_crowd(self, name, places, bounds, kinds, groups=(), seed=1):
        """
        A crowd (include/dgx_ge_crowd.h): actors of kinds that live by their scripts, its blob named so.
          places: how many actors at once; bounds: (x, y, w, h) of the world, points of the scene (solid beyond);
          seed: of its dice (where the groups are strewn, rnd of its actors);
          kinds: [{"kind": an animation added before, "side": a number (who meets whom: own or foreign masks), "layer"
            (of the scene), "damage": a param of it a hit's damage is taken off (none: none), "color" (of pictures of 1 bit),
            "by_y": its key in the layer is its y (a world seen from above)}];
          groups: [{"kind": one of the kinds, "script" (its first: none), "count", "area": (x, y, w, h) they are strewn in,
            "flip": False, True or "either"}].
        """
        blobs = {n: i for i, (n, _) in enumerate(self.blobs)}
        anims = {a[0]: a for a in self.anims}
        x, y, w, h = bounds
        if not 1 <= places <= 0xFFFF or w <= 0 or h <= 0:
            raise ValueError(f"crowd {name}: places 1..65535 and bounds of some size")
        out, entries = bytearray(), []
        for k in kinds:
            if k["kind"] not in anims:
                raise ValueError(f"crowd {name}: no animation {k['kind']} (add it before the crowd)")
            if k["kind"] in entries:
                raise ValueError(f"crowd {name}: the kind {k['kind']} twice")
            params = anims[k["kind"]][3]
            damage = k.get("damage")
            if damage is not None and damage not in params:
                raise ValueError(f"crowd {name}: {k['kind']} has no param {damage}")
            entries.append(k["kind"])
            out += struct.pack("<HHBBBB", blobs[k["kind"]], k.get("side", 0), k.get("layer", 0), 0xFF if damage is None else params.index(damage),
                               k.get("color", 0), 1 if k.get("by_y") else 0)
        for g in groups:
            if g["kind"] not in entries:
                raise ValueError(f"crowd {name}: a group of {g['kind']}, not a kind of it")
            scripts = anims[g["kind"]][1]
            script = g.get("script", scripts[0])
            if script not in scripts:
                raise ValueError(f"crowd {name}: no script {script} of {g['kind']}")
            ax, ay, aw, ah = g.get("area", bounds)
            flip = g.get("flip", False)
            out += struct.pack("<BBHhhHHBxxx", entries.index(g["kind"]), scripts.index(script), g.get("count", 1), ax, ay, aw, ah,
                               2 if flip == "either" else 1 if flip else 0)
        head = struct.pack("<IHHHHhhHHI", 0x52434744, 1, places, len(kinds), len(groups), x, y, w, h, seed & 0xFFFFFFFF)
        return self.add_blob(name, head + bytes(out))

    def add_chase(self, kind, sense="always", sides=(), hearing=0, near=None, steps=0, calm=0):
        """
        How a kind (an animation added before) chases the hero out of its scene (include/dgx_ge_chase.h): its sense
        ("sight", "hearing", "always", "none"), the sides it follows by ("left", "right", "up", "down", "doors"), how near
        the hero it hears (hearing), how near a side it must be to follow by it (near: {side: n}; below 0: past it; not
        given: anywhere), how many scenes from home it goes (steps, 0 no end), how long out of its senses before it calms
        down (calm: ms, "5s"; 0 never). All of them go into one blob, CHASE, written with the pack.
        """
        senses = ("none", "sight", "hearing", "always")
        bit = {"left": 1, "right": 2, "up": 4, "down": 8, "doors": 16}
        if kind not in {n for n, _ in self.blobs}:
            raise ValueError(f"chase {kind}: no animation {kind} (add it before)")
        if sense not in senses or any(s not in bit for s in sides) or any(s not in bit or s == "doors" for s in (near or {})):
            raise ValueError(f"chase {kind}: sense {senses}, sides {tuple(bit)}")
        if isinstance(calm, str):
            m = re.fullmatch(r"(\d+)\s*(ms|s)", calm.strip())
            if not m:
                raise ValueError(f"chase {kind}: calm is a time, 500ms or 5s")
            calm = int(m.group(1)) * (1000 if m.group(2) == "s" else 1)
        if not 0 <= calm < 65536 or not 0 <= steps < 256:
            raise ValueError(f"chase {kind}: calm up to 65535 ms, steps up to 255")
        near = [(near or {}).get(s, 32767) for s in ("left", "right", "up", "down")]
        kind_id = [n for n, _ in self.blobs].index(kind)
        if any(k == kind_id for k, _ in self.chases):
            raise ValueError(f"chase {kind} twice")
        self.chases.append((kind_id, struct.pack("<HBBh4hBxH", kind_id, senses.index(sense), sum(bit[s] for s in sides), hearing, *near, steps, calm)))

    def add_meets(self, text):
        """
        Which masks meet (dgx_ge_meets_t), a pair a line, '#' to the end of a
        line is a comment:

            foreign BLADE BODY    # a blade of one side cuts a body of another
            foreign BLADE PARRY
            own STONE BODY        # a thrown stone may fall on its thrower

        own: bodies of one side, foreign: of different sides. A mask by its
        name (name_mask, before) or its index; the pair meets either way round.
        A pair not given does not meet. Given at all, these lines are all:
        nothing else meets. A pair given twice is an error.
        """
        names = {n: i for i, n in self.mask_names.items()}
        rows = {"own": [0] * 16, "foreign": [0] * 16}
        for number, raw in enumerate(text.splitlines(), 1):
            line = raw.split("#", 1)[0].split()
            if not line:
                continue
            if len(line) != 3 or line[0] not in rows:
                raise ValueError(f"meets, line {number}: 'own|foreign <mask> <mask>', not '{raw.strip()}'")
            pair = []
            for word in line[1:]:
                index = int(word) if word.isdigit() else names.get(name_of(word))
                if index is None or not 0 <= index <= 15:
                    raise ValueError(f"meets, line {number}: no mask {word}")
                pair.append(index)
            a, b = pair
            row = rows[line[0]]
            if row[a] >> b & 1:
                raise ValueError(f"meets, line {number}: {line[0]} {line[1]} {line[2]} given twice")
            row[a] |= 1 << b
            row[b] |= 1 << a
        self.meets = rows

    def name_mask(self, index, name):
        """The name of a mask index, for the header: <PREFIX>_MASK_<NAME>."""
        self.mask_names[index] = name

    def add_sound(self, name, wav=None, samples=None, rate=0, loop=False, codec="adpcm", stereo=False, side=1, gain=1.0):
        """
        A sound by its name, the next number (dgx_ge_sound.h): the header gets <PREFIX>_SOUND_<NAME>. Its samples, if
        any: a WAV file (PCM of 8 or 16 bits, mono or stereo, any rate) or samples given (signed 16-bit, at rate: a
        list of values, or of (left, right) pairs for stereo); kept mono unless stereo (a stereo one is then kept so,
        a mono one is mono anyway); as IMA ADPCM, 4 bits a sample (codec "adpcm"), or 8 bits unsigned ("pcm8"); loop:
        it goes round until stopped. Stereo is kept as the middle (L + R) / 2 at the full rate and the side (L - R) / 2
        at a 2^side-th of it (side=1: half; the two of one signal differ little), side=None: left and right as they
        are; channels that hardly differ are kept mono. gain: its loudness against the recording (0.5 half), made into
        its samples here (nothing at play). Without samples it is only a name: it plays nothing. Returns its number.
        """
        if name in self.sounds:
            raise ValueError(f"sound {name} twice")
        if wav is not None:
            samples, rate = read_wav(wav, stereo)
        if samples is not None and gain != 1.0:
            def g(v):
                return max(-32768, min(32767, int(round(v * gain))))
            samples = [(g(v[0]), g(v[1])) if isinstance(v, (tuple, list)) else g(v) for v in samples]
        if samples is not None and len(samples) and isinstance(samples[0], (tuple, list)):
            # stereo made mono when it is not asked for, or when its channels differ by less than a step of 8 bits
            if not stereo or max(abs(l - r) for l, r in samples) < 256:
                samples = [(l + r) // 2 for l, r in samples]
        if side is not None and not 0 <= side <= 3:
            raise ValueError(f"sound {name}: side 0..3 (its rate the sound's / 2^side) or None, not {side}")
        if samples is not None and (not rate or rate > 65535):
            raise ValueError(f"sound {name}: a rate of 1..65535, not {rate}")
        if codec not in SOUND_CODECS:
            raise ValueError(f"sound {name}: codec adpcm or pcm8, not {codec}")
        self.sounds.append(name)
        self.sound_data.append((list(samples), rate, loop, codec, side) if samples is not None and len(samples) else None)
        return len(self.sounds) - 1

    def _sounds_blob(self):
        """
        SOUNDS: 'DGSN', version 1, the count; for each sound 12 bytes: the offset of its data from the start of the
        blob, its samples, its rate, its codec (bits 0-3; bit 5: middle and side, bit 6: stereo, bit 7: it goes
        round), the side's rate as a shift (its samples: samples >> shift, rounded up); then the data: a stream for
        each channel one after the other (left and right, or the middle and the side), an ADPCM sound from the
        predictor (int16), step index (byte) and a byte 0 of each stream, then each stream's nibbles from a byte of
        its own (the first in the low half).
        """
        head = struct.pack("<IHH", 0x4E534744, 1, len(self.sounds))
        data, entries = b"", []
        at = len(head) + 12 * len(self.sounds)
        for d in self.sound_data:
            if d is None:
                entries.append(struct.pack("<IIHBB", 0, 0, 0, 0, 0))
                continue
            samples, rate, loop, codec, side = d
            stereo, shift = isinstance(samples[0], (tuple, list)), 0
            if not stereo:
                streams = [samples]
            elif side is None:
                streams = [[l for l, _ in samples], [r for _, r in samples]]
            else:
                # the side at a 2^side-th of the rate: the mean of each run of its samples
                shift, mid, dif = side, [(l + r) // 2 for l, r in samples], [(l - r) // 2 for l, r in samples]
                k = 1 << shift
                streams = [mid, [sum(dif[i:i + k]) // len(dif[i:i + k]) for i in range(0, len(dif), k)]]
            if codec == "adpcm":
                coded = [ima_adpcm_stream(st) for st in streams]
                body  = b"".join(struct.pack("<hBB", p, i, 0) for (p, i), _ in coded) + b"".join(n for _, n in coded)
            else:
                body = b"".join(bytes((max(-32768, min(32767, v)) >> 8) + 128 for v in st) for st in streams)
            flags = SOUND_CODECS[codec] | (0x20 if stereo and side is not None else 0) | (0x40 if stereo else 0) | (0x80 if loop else 0)
            entries.append(struct.pack("<IIHBB", at + len(data), len(samples), rate, flags, shift))
            data += body
        return head + b"".join(entries) + data

    def add_blob(self, name, data):
        """Bytes the game makes sense of itself; returns its number. A name once: it is a number of the header."""
        if any(n == name for n, _ in self.blobs):
            raise ValueError(f"a blob named {name} twice (an animation and a dialog of one name?)")
        self.blobs.append((name, bytes(data)))
        return len(self.blobs) - 1

    def add_anim(self, name, text):
        """
        Scripts of animation (see dgx_ge_anim.py) as a blob; the pictures they
        name are those added before. Events are the pack's: one name, one
        number for every kind, so a hit of one kind means the same to another.
        The header gets the numbers of the scripts, events and params:
        <PREFIX>_<NAME>_SCRIPT_<script>, <PREFIX>_<NAME>_EVENT_<event> (and
        <PREFIX>_EVENT_<event>), <PREFIX>_<NAME>_PARAM_<param>, and the
        questions to the game it declares: <PREFIX>_<NAME>_GAME_<name>.
        """
        from dgx_ge_anim import assemble

        images = {im[0]: i for i, im in enumerate(self.images)}
        # points by the names the scripts give them (as they are written), and as the header knows them
        points = dict(self.point_names, **{n.lower(): v for n, v in self.point_names.items()})
        # kinds (shots, draw, count) by the names of the blobs added before
        kinds = {n: i for i, (n, _) in enumerate(self.blobs)}
        # the params of the kinds added before (its. of a sense of them), and this one as a kind of its own
        kind_params = {n: list(ps) for n, _, _, ps in self.anims}
        done = assemble(text, images, points, self.events, kinds, self.timers, kind_params, (name, len(self.blobs)))
        self.anims.append((name, done.scripts, done.events, done.params))
        self.games[name] = done.games
        return self.add_blob(name, done.blob)

    def add_anims(self, anims):
        """
        Animations that sense one another (a wolf and the sheep it hunts): [(name, text)], as add_anim each, but every
        one knows the others as kinds (sense, shoot, appear, count) and their params (its.), whatever their order:
        their blobs are numbered first, in this order, then each is made. Returns their numbers.
        """
        from dgx_ge_anim import assemble

        anims = list(anims)
        names = [n for n, _ in anims]
        if len(set(names)) != len(names) or any(n == b for n in names for b, _ in self.blobs):
            raise ValueError(f"animations {names}: a name twice")
        first = len(self.blobs)
        for name in names:
            self.blobs.append((name, None))  # a number now, the blob below
        images = {im[0]: i for i, im in enumerate(self.images)}
        points = dict(self.point_names, **{n.lower(): v for n, v in self.point_names.items()})
        kinds = {n: i for i, (n, _) in enumerate(self.blobs)}
        kind_params = {n: list(ps) for n, _, _, ps in self.anims}
        for name, text in anims:  # the params each declares, in order: param <name> <value>
            kind_params[name] = [m.group(1) for m in (re.match(r"\s*param\s+(\w+)\b", line.split("#")[0]) for line in text.splitlines()) if m]
        for i, (name, text) in enumerate(anims):
            done = assemble(text, images, points, self.events, kinds, self.timers, kind_params, (name, first + i))
            self.anims.append((name, done.scripts, done.events, done.params))
            self.games[name] = done.games
            self.blobs[first + i] = (name, done.blob)
        return list(range(first, first + len(anims)))

    def translate(self, po_path):
        """The language of the pack: translations of msgid in contexts (a gettext .po). Before the dialogs."""
        from dgx_ge_dialog import read_po
        self.strings.translations.update(read_po(po_path))

    def add_dialog(self, text):
        """
        A dialog (see dgx_ge_dialog.py) as a blob named as the dialog; the kinds
        its roles name are animations added before. Its strings go into the
        strings of the pack (one blob, STRINGS, added when the pack is written).
        The header gets <PREFIX>_<DIALOG>_ROLE_<role> and <PREFIX>_STYLE_<style>.
        """
        from dgx_ge_dialog import assemble_dialog
        kinds = {n: i for i, (n, _) in enumerate(self.blobs)}
        params = {name: {p: (i, 0) for i, p in enumerate(ps)} for name, _, _, ps in self.anims}
        done = assemble_dialog(text, self.strings, kinds, params, self.events, self.styles, {im[0]: i for i, im in enumerate(self.images)},
                               self.timers)
        self.dialogs.append((done.name, done.roles))
        return self.add_blob(name_of(done.name), done.blob)

    def add_font(self, name):
        """A font of the game by name, in the table in this order (<PREFIX>_FONT_<NAME>); styles name it; the game gives it."""
        if name not in self.fonts:
            self.fonts.append(name)
        return self.fonts.index(name)

    def add_style(self, name, **f):
        """
        How a dialog in this style looks (see include/dgx_ge_talk_view.h). Fields, all may be left out:
          frame "fill" | "picture" | "none"; picture (a picture of the pack), insets [l, t, r, b];
          place [x, y, w, h] (x, y below 0: from the right, the bottom; w, h 0 or below: the frame's size less so much);
          padding [l, t, r, b]; font (a name of the game's fonts); scale [max, min]; important (bool);
          scroll_ms, wait_ms; fill, border, text, bar, name_color: colors (r, g, b); border_width;
          appear "now" | "letters", letter_ms (a letter every so many ms); show_name (the name of the one speaking above the line);
          pen "soft" | "hard" (a handwritten font: soft, in 8 bits a point; a font of pixels is 1 bit whatever it says);
          portrait "left" | "right" | "none", portrait_width (the room for it: the text narrower by so much and the left padding);
          align "left" | "middle" | "right" (each line of the text in its field).
        A frame of a picture: its insets cut it into nine pictures (corners, edges, the middle; <STYLE>_FRAME_0..8, row
        after row), the corners drawn as they are, the edges and the middle laid again and again; each at least a point.
        """
        if f.get("frame") == "picture":
            f = dict(f, picture_first=self._cut_frame(name, f.get("picture"), f.get("insets", (0, 0, 0, 0))))
        if name not in self.styles:
            self.styles.append(name)
        if f.get("font") and f["font"] not in self.fonts:
            self.fonts.append(f["font"])
        self.style_data[name] = f

    def add_crop(self, name, picture, x, y, width, height, origin=(0, 0)):
        """A part of a picture of the pack as a picture of its own, of the same set (a head out of a frame: a portrait); its number."""
        found = [im for im in self.images if im[0] == picture]
        if not found:
            raise ValueError(f"crop {name}: no picture {picture}")
        _, set_id, w, h, _, _, bits, data = found[0]
        if x < 0 or y < 0 or width < 1 or height < 1 or x + width > w or y + height > h:
            raise ValueError(f"crop {name}: {x}, {y}, {width} x {height} is not inside {picture} ({w} x {h})")
        values = unpack_pixels(data, w, h, bits)
        return self.add_image(name, set_id, width, height, [values[(y + j) * w + x + i] for j in range(height) for i in range(width)], bits, origin)

    def _cut_frame(self, style, picture, insets):
        """The nine pictures of a frame cut out of a picture of the pack by insets l, t, r, b: the number of the first."""
        found = [i for i, im in enumerate(self.images) if im[0] == picture]
        if not found:
            raise ValueError(f"style {style}: no picture {picture} for its frame")
        name, set_id, w, h, ox, oy, bits, data = self.images[found[0]]
        values = unpack_pixels(data, w, h, bits)
        l, t, r, b = insets
        if min(l, t, r, b) < 1 or w - l - r < 1 or h - t - b < 1:
            raise ValueError(f"style {style}: insets {insets} do not cut {picture} ({w} x {h}) into nine pictures of a point at least")
        cols, rows = ((0, l), (l, w - r), (w - r, w)), ((0, t), (t, h - b), (h - b, h))
        first = len(self.images)
        for i, (y0, y1) in enumerate(rows):
            for j, (x0, x1) in enumerate(cols):
                piece = [values[y * w + x] for y in range(y0, y1) for x in range(x0, x1)]
                self.add_image(f"{name_of(style)}_FRAME_{3 * i + j}", set_id, x1 - x0, y1 - y0, piece, bits, (0, 0))
        return first

    def _styles_blob(self):
        images = {im[0]: i for i, im in enumerate(self.images)}
        out = struct.pack("<IHH", 0x54534744, 1, len(self.styles))  # "DGST", version, count
        for name in self.styles:
            f = self.style_data.get(name)
            if f is None:
                out += bytes([0xFF]) + bytes(47)  # named, not made: the engine's default
                continue
            frame = {"none": 0, "fill": 1, "picture": 2}[f.get("frame", "fill")]
            appear = {"now": 0, "letters": 1}[f.get("appear", "now")] | (0x80 if f.get("pen") == "soft" else 0)
            picture = f["picture_first"] if f.get("picture_first") is not None else images[f["picture"]] if f.get("picture") else 0xFFFF
            x, y, w, h = f.get("place", (4, -4, -8, 44))
            hi, lo = f.get("scale", (1, 1))

            def rgb(k, d):
                return bytes(f.get(k, d))[:3]
            out += struct.pack("<BBH4B4h4BBBBBHH", frame, appear, picture, *f.get("insets", (0, 0, 0, 0)), x, y, w, h, *f.get("padding", (6, 4, 6, 4)),
                               self.fonts.index(f["font"]) if f.get("font") else 0, hi, lo, 1 if f.get("important") else 0,
                               f.get("scroll_ms", 1200), f.get("wait_ms", 0))
            out += rgb("fill", (16, 16, 48)) + bytes([f.get("border_width", 1)]) + rgb("border", (200, 200, 220)) + bytes([f.get("letter_ms", 40)])
            side = {"none": 0, "left": 1, "right": 2}[f.get("portrait", "none")]
            out += rgb("text", (240, 240, 240)) + bytes([side]) + rgb("bar", (64, 64, 140)) + bytes([f.get("portrait_width", 0)])
            out += rgb("name_color", (255, 220, 120))
            align = {"left": 0, "middle": 1, "right": 2}[f.get("align", "left")]
            out += bytes([(1 if f.get("show_name") else 0) | align << 1])
        return out

    def write_pot(self, path):
        """Every msgid of the dialogs in its context: the template a translation is made from."""
        with open(path, "w", encoding="utf-8") as f:
            f.write(self.strings.pot())

    def build(self):
        tables = len(self.sets) * 8 + len(self.images) * 16 + len(self.blobs) * 8 + len(self.masks) * 16 + len(self.points) * 8
        data_at = HEADER_BYTES + tables
        data = bytearray()

        def place(chunk):
            at = data_at + len(data)
            data.extend(_align4(chunk))
            return at

        sets = b"".join(struct.pack("<IHH", place(b"".join(bytes(c) for c in colors)), len(colors), 0) for _, colors in self.sets)
        images = b"".join(
            struct.pack("<HHhhBBHI", w, h, ox, oy, bits, 0, s, place(pixels)) for _, s, w, h, ox, oy, bits, pixels in self.images
        )
        blobs = b"".join(struct.pack("<II", place(blob), len(blob)) for _, blob in self.blobs)
        masks = b"".join(struct.pack("<HBBhhHHI", im, index, 0, x, y, w, h, place(bits))
                         for im, index, x, y, w, h, bits in sorted(self.masks, key=lambda m: (m[0], m[1])))
        points = b"".join(struct.pack("<HBbhh", im, number, z, x, y) for (im, number), (x, y, z) in sorted(self.points.items()))
        sets_at = HEADER_BYTES
        images_at = sets_at + len(sets)
        blobs_at = images_at + len(images)
        masks_at = blobs_at + len(blobs)
        points_at = masks_at + len(masks)
        meets_at = place(struct.pack("<32H", *self.meets["own"], *self.meets["foreign"])) if self.meets else 0
        size = data_at + len(data)
        header = struct.pack("<IHHIHHHHIIIIIHHI", MAGIC, VERSION, HEADER_BYTES, size, len(self.sets), len(self.images), len(self.blobs),
                             len(self.masks), sets_at, images_at, blobs_at, masks_at, points_at, len(self.points), 0, meets_at)
        pack = bytearray(header + struct.pack("<I", 0) + sets + images + blobs + masks + points + bytes(data))
        # its fingerprint: the CRC-32 of the whole pack, the fingerprint 0 meanwhile (a save of a game knows its data by it)
        struct.pack_into("<I", pack, 48, zlib.crc32(pack))
        return bytes(pack)

    def header(self, guard):
        lines = [f"/* Made by dgx_ge_pack.py: the numbers of {self.prefix.lower()}.pack. Do not edit. */", "#pragma once", ""]
        for kind, items in (("SET", self.sets), ("IMG", self.images), ("BLOB", self.blobs)):
            for i, item in enumerate(items):
                lines.append(f"#define {self.prefix}_{kind}_{item[0]} {i}")
            lines.append(f"#define {self.prefix}_{kind}_COUNT {len(items)}")
            lines.append("")
        for name, tiles, slots in self.tilesets:
            for i, t in enumerate(tiles):
                lines.append(f"#define {self.prefix}_{name}_TILE_{name_of(t)} {i + 1}")
            for i, sl in enumerate(slots):
                lines.append(f"#define {self.prefix}_{name}_SLOT_{name_of(sl)} {i}")
            lines.append("")
        for i, (n, _) in enumerate(self.game):
            lines.append(f"#define {self.prefix}_GAME_{name_of(n)} {i}")
        if self.game:
            lines.append("")
        for g in self.graphs:
            for kind, names in (("EXIT", g.exit_names), ("ENTRY", g.entry_names), ("MAP", [m[0] for m in g.maps])):
                for i, n in enumerate(names):
                    lines.append(f"#define {self.prefix}_{name_of(g.name)}_{kind}_{name_of(n)} {i}")
            lines.append("")
        for index, name in sorted(self.mask_names.items()):
            lines.append(f"#define {self.prefix}_MASK_{name} {index}")
        if self.mask_names:
            lines.append("")
        for name, number in self.point_names.items():
            lines.append(f"#define {self.prefix}_POINT_{name} {number}")
        if self.point_names:
            lines.append("")
        for i, n in enumerate(self.sounds):
            lines.append(f"#define {self.prefix}_SOUND_{name_of(n)} {i}")
        if self.sounds:
            lines.append(f"#define {self.prefix}_SOUND_COUNT {len(self.sounds)}")
            lines.append("")
        for i, n in enumerate(self.timers):
            lines.append(f"#define {self.prefix}_TIMER_{name_of(n)} {i}")
        if self.timers:
            lines.append("")
        for i, e in enumerate(self.events):
            lines.append(f"#define {self.prefix}_EVENT_{e.upper()} {i}")
        if self.events:
            lines.append("")
        if self.anims:
            # the params an actor of the pack keeps at most: the engine is built with DGX_GE_ANIM_PARAMS no fewer
            # (dgx_ge.cmake: dgx_ge_params_of_pack); an animation with more than it keeps does not open. Two at least:
            # the lives of cells keep a cell's modifier and tile in the first two (dgx_ge_cells.c)
            lines += [f"#define {self.prefix}_ANIM_PARAMS {max(max(len(p) for *_, p in self.anims), 2)}", ""]
        for name, scripts, events, params in self.anims:
            for kind, items in (("SCRIPT", scripts), ("EVENT", events), ("PARAM", params), ("GAME", self.games.get(name, ()))):
                for i, item in enumerate(items):
                    lines.append(f"#define {self.prefix}_{name}_{kind}_{item.upper()} {i}")
            lines.append("")
        for i, s in enumerate(self.styles):
            lines.append(f"#define {self.prefix}_STYLE_{name_of(s)} {i}")
        for i, s in enumerate(self.fonts):
            lines.append(f"#define {self.prefix}_FONT_{name_of(s)} {i}")
        if self.fonts:
            lines.append(f"#define {self.prefix}_FONT_COUNT {len(self.fonts)}")  # the game's fonts by these numbers: so many
        for name, roles in self.dialogs:
            for i, r in enumerate(roles):
                lines.append(f"#define {self.prefix}_{name_of(name)}_ROLE_{name_of(r)} {i}")
        return "\n".join(lines)

    def write(self, pack_path, header_path=None, sounds_path=None):
        """
        The pack, and its header. sounds_path: the samples of the sounds (the blob SOUNDS) written there apart, not in
        the pack: a board without sound leaves them out of its flash; the sounds' names and numbers stay in the header.
        """
        if self.strings.texts and "STRINGS" not in (n for n, _ in self.blobs):
            self.add_blob("STRINGS", self.strings.blob())  # the strings of the dialogs, once, last
        if self.game and "GAME" not in (n for n, _ in self.blobs):
            self.add_blob("GAME", struct.pack("<IHH", 0x4D474744, 1, len(self.game)) + b"".join(struct.pack("<i", v) for _, v in self.game))
        if self.chases and "CHASE" not in (n for n, _ in self.blobs):
            self.add_blob("CHASE", struct.pack("<IHH", 0x48434744, 1, len(self.chases)) + b"".join(r for _, r in self.chases))
        if self.timers and "TIMERS" not in (n for n, _ in self.blobs):
            self.add_blob("TIMERS", self._timers_blob())   # the timers of the game, by their names
        if self.styles and "STYLES" not in (n for n, _ in self.blobs):
            self.add_blob("STYLES", self._styles_blob())   # the styles dialogs name
        if any(self.sound_data) and sounds_path:
            with open(sounds_path, "wb") as f:
                f.write(self._sounds_blob())                # the samples of the sounds apart: a file of the board that plays them
        elif any(self.sound_data) and "SOUNDS" not in (n for n, _ in self.blobs):
            self.add_blob("SOUNDS", self._sounds_blob())   # the samples of the sounds, one blob
        blob = self.build()
        with open(pack_path, "wb") as f:
            f.write(blob)
        if header_path:
            with open(header_path, "w") as f:
                f.write(self.header(self.prefix))
        for w in self.warnings:
            print("warning:", w, file=sys.stderr)
        return len(blob)
