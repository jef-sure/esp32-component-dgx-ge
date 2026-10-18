#!/usr/bin/env python3
"""
Fonts by rules: which faces may become a pixel font of DGX at which sizes, as the person says in a text file, and the
pick of one for a size, made by DGX's font2c.

    dgx_ge_fonts.py pick RULES SIZE [--mono] [--family PATTERN]
        the font the rules give for SIZE pixels of the display
    dgx_ge_fonts.py list RULES SIZE [--mono] [--family PATTERN]
        every font they allow at SIZE, in the order they would be taken
    dgx_ge_fonts.py make RULES SIZE OUTDIR [--mono] [--family PATTERN] [--chars FIRST LAST ...]
        the one picked, made by font2c into OUTDIR (.c and .h)
    dgx_ge_fonts.py table NEEDS RULES SCALE OUTDIR [--root DIR] [--text FILE ...]
        the fonts a game needs, made for a board that shows its frame SCALE times larger: OUTDIR/font_<name>.c and .h
        for each one picked by the rules (its function board_font_<name>), and OUTDIR/board_fonts.c, board_fonts(&count,
        &made) giving them in the order of the needs, made[i] whether one was made at the scale (a file: as it is) (a file is rewritten only when it changes); their letters: ASCII and
        every other one the texts have (the game's strings, its translations), or without texts ASCII, Latin-1, Russian

The needs of a game, a font a line, in the order of its table (# a comment):

    text 12 mono                         a name, its size in points of the frame, mono, a family (a pattern)
    note 14 family "Arial*"
    time 8 file "main/fonts/PxCgaThin8.c" a font as it is, at every scale (a path from --root, built elsewhere)

The rules, a line each (# a comment):

    source list ~/fonts.txt        a file naming fonts, a path a line
    source dir  ~/Fonts            every .ttf, .otf, .ttc under it, deep
    source system                  the fonts installed (fontconfig)
    allow "Terminus*"  > 12 & < 40 a family (or "family style"; * any run of letters; case aside) at these sizes
    allow "DejaVu Sans Mono" 16..  sizes: 14 | 16..24 | 16.. | ..24 | > 12 | >= 12 | < 40 | <= 40, & is and,
    deny  "Comic*"                 a comma or a space between them is or; deny without sizes: at none
    allow * 40..

The allow lines are in the order of liking: the first one with a font of the size wins; among its fonts the plain
face before bold and slanted ones, then the order of the sources. A face that is denied at the size is never taken.
"""
import argparse
import fnmatch
import os
import re
import shutil
import subprocess
import sys
from collections import namedtuple

Font = namedtuple("Font", "file family style mono order")
Rule = namedtuple("Rule", "allow pattern sizes where")


class RulesError(Exception):
    pass


FONT_FILES = (".ttf", ".otf", ".ttc")
PLAIN = ("regular", "book", "medium", "normal", "roman", "plain")
FC_FORMAT = "%{file}|%{index}|%{family[0]}|%{style[0]}|%{spacing}\\n"
# what a font is made with when not told: ASCII, Latin-1, the Russian letters and ё
DEFAULT_CHARS = ["0x20", "0x7e", "0xa0", "0xff", "0x401", "0x401", "0x410", "0x44f", "0x451", "0x451"]


def parse_sizes(text, where):
    """Sizes as alternatives, each a list of conditions (op, n) that hold together: [[(">", 12), ("<", 40)], ...]."""
    tokens = re.findall(r">=|<=|>|<|&|,|\.\.|\d+|\S", text)
    alternatives, i = [], 0

    def number():
        nonlocal i
        if i < len(tokens) and tokens[i].isdigit():
            i += 1
            return int(tokens[i - 1])
        raise RulesError(f"{where}: a number expected in sizes '{text}'")

    def condition():
        nonlocal i
        tok = tokens[i] if i < len(tokens) else None
        if tok in (">", ">=", "<", "<="):
            i += 1
            return [(tok, number())]
        if tok == "..":
            i += 1
            return [("<=", number())]
        low = number()
        if i < len(tokens) and tokens[i] == "..":
            i += 1
            if i < len(tokens) and tokens[i].isdigit():
                return [(">=", low), ("<=", number())]
            return [(">=", low)]
        return [("==", low)]

    while i < len(tokens):
        if tokens[i] == ",":
            i += 1
            continue
        conds = condition()
        while i < len(tokens) and tokens[i] == "&":
            i += 1
            if i >= len(tokens):
                raise RulesError(f"{where}: nothing after & in sizes '{text}'")
            conds += condition()
        alternatives.append(conds)
    return alternatives


def size_allowed(sizes, size):
    """Whether size is one of the sizes (None: all of them)."""
    if sizes is None:
        return True
    ops = {"==": lambda a, b: a == b, ">": lambda a, b: a > b, ">=": lambda a, b: a >= b, "<": lambda a, b: a < b, "<=": lambda a, b: a <= b}
    return any(all(ops[op](size, n) for op, n in conds) for conds in sizes)


def parse_rules(text, name="rules"):
    """The sources [(kind, path or None)] and the rules [Rule] of a text of rules."""
    sources, rules = [], []
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        where = f"{name}:{number}"
        word, _, rest = line.partition(" ")
        rest = rest.strip()
        if word == "source":
            kind, _, path = rest.partition(" ")
            path = path.strip()
            if kind == "system" and not path:
                sources.append(("system", None))
            elif kind in ("list", "dir") and path:
                sources.append((kind, os.path.expanduser(path)))
            else:
                raise RulesError(f"{where}: source list <file>, source dir <dir> or source system")
        elif word in ("allow", "deny"):
            m = re.fullmatch(r'"([^"]+)"\s*(.*)|(\*)\s*(.*)', rest)
            if not m:
                raise RulesError(f'{where}: {word} "<family>" <sizes> (or * for any family)')
            pattern, sizes = (m.group(1), m.group(2)) if m.group(1) else (m.group(3), m.group(4))
            if word == "allow" and not sizes.strip():
                raise RulesError(f"{where}: allow without sizes: say which (16.. for all from 16)")
            rules.append(Rule(word == "allow", pattern.lower(), parse_sizes(sizes, where) if sizes.strip() else None, where))
        else:
            raise RulesError(f"{where}: source, allow or deny, not '{word}'")
    return sources, rules


def matches(pattern, font):
    """The pattern of a rule against the family, or the family and the style."""
    return fnmatch.fnmatchcase(font.family.lower(), pattern) or fnmatch.fnmatchcase(f"{font.family} {font.style}".lower(), pattern)


def candidates(fonts, rules, size, mono=False, family=None):
    """Every font the rules allow at size, in the order they would be taken: [(rule, font)]."""
    family = family.lower() if family else None
    denied = [r for r in rules if not r.allow and size_allowed(r.sizes, size)]
    out, seen = [], set()
    for rule in rules:
        if not rule.allow or not size_allowed(rule.sizes, size):
            continue
        found = [f for f in fonts if matches(rule.pattern, f) and not any(matches(d.pattern, f) for d in denied) and (f.mono or not mono)
                 and (not family or matches(family, f)) and f.file not in seen]
        found.sort(key=lambda f: (0 if f.style.lower() in PLAIN else 1, f.order))
        for f in found:
            seen.add(f.file)
            out.append((rule, f))
    return out


def scan(files, order0):
    """Fonts of files by fontconfig: their family, style, whether every letter is as wide (face 0 of a collection)."""
    fonts = []
    for k in range(0, len(files), 200):
        run = subprocess.run(["fc-scan", "--format", FC_FORMAT] + files[k:k + 200], capture_output=True, text=True)
        fonts += read_fc(run.stdout, order0 + len(fonts))
    return fonts


def read_fc(text, order0=0):
    fonts = []
    for line in text.splitlines():
        parts = line.split("|")
        if len(parts) != 5 or parts[1] != "0" or not parts[0].lower().endswith(FONT_FILES):
            continue  # font2c makes the first face of a file
        fonts.append(Font(parts[0], parts[2], parts[3], parts[4] == "100", order0 + len(fonts)))
    return fonts


def gather(sources):
    """The fonts of the sources, in their order, each file once."""
    fonts, seen = [], set()
    for kind, path in sources:
        if kind == "system":
            run = subprocess.run(["fc-list", "--format", FC_FORMAT], capture_output=True, text=True)
            got = sorted(read_fc(run.stdout), key=lambda f: f.file)
        elif kind == "dir":
            files = sorted(os.path.join(d, n) for d, _, names in os.walk(path) for n in names if n.lower().endswith(FONT_FILES))
            got = scan(files, 0)
        else:
            try:
                lines = open(path, encoding="utf-8").read().splitlines()
            except OSError as e:
                raise RulesError(f"source list {path}: {e.strerror}")
            files = [os.path.expanduser(l.strip()) for l in lines if l.strip() and not l.strip().startswith("#")]
            got = scan([f for f in files if os.path.exists(f)], 0)
        for f in got:
            if f.file not in seen:
                seen.add(f.file)
                fonts.append(f._replace(order=len(fonts)))
    return fonts


Need = namedtuple("Need", "name points mono family file where")


def parse_needs(text, name="needs"):
    """The needs of a game, in the order of its table: [Need]."""
    needs = []
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        where = f"{name}:{number}"
        m = re.fullmatch(r'([A-Za-z_]\w*)\s+(\d+)((?:\s+mono)?)(?:\s+family\s+"([^"]+)")?(?:\s+file\s+"([^"]+)")?', line)
        if not m or (m.group(5) and (m.group(3) or m.group(4))):
            raise RulesError(f'{where}: <name> <points> [mono] [family "<pattern>"], or <name> <points> file "<path>"')
        if m.group(1) in (n.name for n in needs):
            raise RulesError(f"{where}: {m.group(1)} twice")
        needs.append(Need(m.group(1), int(m.group(2)), bool(m.group(3)), m.group(4), m.group(5), where))
    return needs


def accessor(c_text, where):
    """The function a font's C file gives it by: dgx_font_t *Name()."""
    m = re.search(r"^dgx_font_t\s*\*\s*(\w+)\s*\(", c_text, re.M)
    if not m:
        raise RulesError(f"{where}: no dgx_font_t *<name>() in it")
    return m.group(1)


def write_if_changed(path, data):
    """A file written only when it is not already so: a build does not make again what did not change."""
    mode = "rb" if isinstance(data, bytes) else "r"
    try:
        with open(path, mode) as f:
            if f.read() == data:
                return
    except OSError:
        pass
    with open(path, "wb" if isinstance(data, bytes) else "w") as f:
        f.write(data)


def table(needs, fonts, rules, scale, outdir, root, dgx, letters=None):
    """The fonts of the needs for a scale into outdir, and board_fonts.c; the lines of what was taken."""
    os.makedirs(outdir, exist_ok=True)
    told, entries, made = [], [], {}
    for n in needs:
        if n.file:
            path = n.file if os.path.isabs(n.file) else os.path.join(root, n.file)
            try:
                name = accessor(open(path, encoding="utf-8", errors="replace").read(), f"{n.where}: {n.file}")
            except OSError as e:
                raise RulesError(f"{n.where}: {n.file}: {e.strerror}")
            entries.append((n, name, f"as it is: {n.file}"))
            told.append(f"{n.name}: {n.file} as it is")
            continue
        size = n.points * scale
        found = candidates(fonts, rules, size, n.mono, n.family)
        if not found:
            raise RulesError(f"{n.where}: {n.name}: no font the rules allow at {size} px{' (mono)' if n.mono else ''}"
                             f"{' of ' + n.family if n.family else ''}")
        rule, f = found[0]
        key = (f.file, size)
        if key not in made:
            work = os.path.join(outdir, "work")
            shutil.rmtree(work, ignore_errors=True)
            os.makedirs(work)
            if letters:
                with open(os.path.join(work, "letters.txt"), "w", encoding="utf-8") as t:
                    t.write(letters)
                chars = ["-f", "letters.txt", f.file, str(size), "0x20", "0x7e"]
            else:
                chars = [f.file, str(size)] + DEFAULT_CHARS
            run = subprocess.run([font2c_binary(dgx)] + chars, cwd=work, capture_output=True, text=True)
            os.remove(os.path.join(work, "letters.txt")) if letters else None
            c = [x for x in os.listdir(work) if x.endswith(".c")]
            h = [x for x in os.listdir(work) if x.endswith(".h")]
            if run.returncode or len(c) != 1 or len(h) != 1:
                raise RulesError(f"{n.where}: font2c failed on {f.file}: {run.stderr.strip() or run.stdout.strip()}")
            c_text = open(os.path.join(work, c[0]), encoding="utf-8", errors="replace").read()
            h_text = open(os.path.join(work, h[0]), encoding="utf-8", errors="replace").read()
            # its function by the need's name: never the name of a font a library has already (DGX has some by font2c)
            given, own = accessor(c_text, f"font2c of {f.file}"), f"board_font_{n.name}"
            c_text, h_text = (re.sub(rf"\b{given}\b", own, x) for x in (c_text, h_text))
            write_if_changed(os.path.join(outdir, f"font_{n.name}.c"), c_text)
            write_if_changed(os.path.join(outdir, f"font_{n.name}.h"), h_text)
            shutil.rmtree(work, ignore_errors=True)
            made[key] = own
        else:
            # the same face and size as an earlier need: its font serves; no second copy of the same symbols
            write_if_changed(os.path.join(outdir, f"font_{n.name}.c"), f"/* {n.name}: the same font as an earlier need */\n")
            write_if_changed(os.path.join(outdir, f"font_{n.name}.h"), "#pragma once\n")
        entries.append((n, made[key], f"{size} px: {f.family} {f.style} ({rule.where}), {f.file}"))
        told.append(f"{n.name}: {size} px, {f.family} {f.style} ({rule.where})")
    lines = [f"/* The fonts of the game for a board at scale {scale}, by dgx_ge_fonts.py table from the needs and the rules: made, not edited. */",
             "#include <stdbool.h>", "", '#include "dgx_font.h"', ""]
    lines += [f"dgx_font_t *{name}(void); /* {n.name}: {what} */" for n, name, what in dict((name, (n, name, what)) for n, name, what in entries).values()]
    made = ", ".join("false" if n.file else "true" for n, _, _ in entries)
    lines += ["", "/* The fonts in the order of the needs (the game's numbers of its fonts), so many; made: each one made at the board's scale (a file: as it is). */",
              "dgx_font_t *const *board_fonts(int *count, const bool **made)", "{", f"    static dgx_font_t *table[{len(entries)}];",
              f"    static const bool at_scale[{len(entries)}] = {{{made}}};", "    if (!table[0]) {"]
    lines += [f"        table[{i}] = {name}();" for i, (n, name, _) in enumerate(entries)]
    lines += ["    }", f"    *count = {len(entries)};", "    *made  = at_scale;", "    return table;", "}", ""]
    write_if_changed(os.path.join(outdir, "board_fonts.c"), "\n".join(lines))
    return told


def font2c_binary(dgx):
    """DGX's font2c, built from its source once (FreeType needed), kept in ~/.cache/dgx-ge."""
    source = os.path.join(dgx, "font2c", "font2c.c")
    built = os.path.join(os.path.expanduser("~/.cache/dgx-ge"), "font2c")
    if os.path.exists(built) and os.path.exists(source) and os.path.getmtime(built) >= os.path.getmtime(source):
        return built
    if not os.path.exists(source):
        raise RulesError(f"no font2c source in {dgx} (--dgx: a DGX checkout or managed component)")
    os.makedirs(os.path.dirname(built), exist_ok=True)
    # the host's compiler with the host's tools: inside an ESP-IDF build its PATH leads to the chips' linkers first
    env = dict(os.environ, PATH=os.pathsep.join(p for p in os.environ.get("PATH", "").split(os.pathsep) if ".espressif" not in p))
    flags = subprocess.run(["pkg-config", "--cflags", "--libs", "freetype2"], capture_output=True, text=True, env=env).stdout.split()
    if subprocess.run(["cc", "-O2", source, "-o", built] + flags, env=env).returncode:
        raise RulesError("font2c does not build (FreeType's headers: libfreetype-dev)")
    return built


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Fonts by rules for DGX (font2c)")
    ap.add_argument("what", choices=("pick", "list", "make", "table"))
    ap.add_argument("rules", help="the rules (table: the needs, then the rules)")
    ap.add_argument("size", help="pixels (table: the rules)")
    ap.add_argument("outdir", nargs="?", help="where (table: the scale)")
    ap.add_argument("tabledir", nargs="?", help="table: where")
    ap.add_argument("--root", default=os.getcwd(), help="table: what the paths of fonts taken as they are start from")
    ap.add_argument("--text", nargs="+", help="table: texts whose letters the fonts must have (beside ASCII)")
    ap.add_argument("--mono", action="store_true", help="every letter as wide")
    ap.add_argument("--family", help="only this family (a pattern, as in the rules)")
    ap.add_argument("--chars", nargs="+", help="ranges for font2c: first last ... (default ASCII, Latin-1, Russian)")
    ap.add_argument("--dgx", default=os.path.join(here, "..", "..", "..", "managed_components", "jef-sure__dgx"), help="DGX with font2c")
    a = ap.parse_args(argv)
    try:
        if a.what == "table":
            if not a.outdir or not a.tabledir or not a.outdir.isdigit():
                raise RulesError("table NEEDS RULES SCALE OUTDIR")
            needs = parse_needs(open(a.rules, encoding="utf-8").read(), a.rules)
            sources, rules = parse_rules(open(a.size, encoding="utf-8").read(), a.size)
            fonts = gather(sources) if any(not n.file for n in needs) else []
            letters = None
            if a.text:
                found = set()
                for t in a.text:
                    found |= {c for c in open(t, encoding="utf-8").read() if ord(c) > 0x7e and c.isprintable()}
                letters = "".join(sorted(found))
            for line in table(needs, fonts, rules, int(a.outdir), a.tabledir, a.root, os.path.abspath(a.dgx), letters):
                print(line)
            return 0
        a.size = int(a.size)
        sources, rules = parse_rules(open(a.rules, encoding="utf-8").read(), a.rules)
        if not sources:
            raise RulesError(f"{a.rules}: no source of fonts")
        found = candidates(gather(sources), rules, a.size, a.mono, a.family)
        if a.what == "list":
            for rule, f in found:
                print(f"{f.family} {f.style}{' (mono)' if f.mono else ''}\t{f.file}\t({rule.where})")
            return 0
        if not found:
            raise RulesError(f"no font the rules allow at {a.size} px{' (mono)' if a.mono else ''}{' of ' + a.family if a.family else ''}")
        rule, f = found[0]
        if a.what == "pick":
            print(f"{f.family} {f.style}{' (mono)' if f.mono else ''}\t{f.file}\t({rule.where})")
            return 0
        if not a.outdir:
            raise RulesError("make: where to? an outdir")
        os.makedirs(a.outdir, exist_ok=True)
        tool = font2c_binary(os.path.abspath(a.dgx))
        run = subprocess.run([tool, f.file, str(a.size)] + (a.chars or DEFAULT_CHARS), cwd=a.outdir, capture_output=True, text=True)
        if run.returncode:
            raise RulesError(f"font2c failed on {f.file}: {run.stderr.strip() or run.stdout.strip()}")
        print(f"{f.family} {f.style}\t{f.file}\t({rule.where})")
        sys.stdout.write(run.stdout)
        return 0
    except (RulesError, OSError) as e:
        print(f"dgx_ge_fonts.py: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
