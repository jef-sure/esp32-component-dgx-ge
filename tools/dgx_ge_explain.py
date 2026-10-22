"""
The reverse interpreter of DGX-GE: what scripts of animation (the text of
dgx_ge_anim.py) do, in words, for people to find their way in them; in
Russian and in English.

    from dgx_ge_explain import explain, explain_pack

    print(explain(text, "ru", words, masks, title="Wine"))
    print(explain_pack({"KID": kid_text, "WINE": wine_text}, "en", words, masks))

A kind: its params, its states (the scripts: what is active, what is done,
how long, what follows; repeats told once), its transitions (on an event,
from where, to where, when; and the events it does not answer). A pack: the
events between kinds, who sends them (hit, recoil, tell holder, contact on)
and who answers them; an event answered and sent by no kind is sent by the
game.

words: {name: {"ru": ..., "en": ...}}: words for the names of events,
params, masks, scripts, points; a name without one stays as it is.
masks: {index: name} of the pack (Pack.mask_names). By templates, no
guessing: every statement of the language has its plain meaning.

From the command line, scripts in files (the kind named by the file, WOLF of wolf.anim), the events between them
first if there are several; markdown to the output:

    python3 dgx_ge_explain.py [--lang ru|en] [--masks 1=BODY,2=JAWS] wolf.anim sheep.anim ...
"""

import re

LANGS = ("ru", "en")

T = {
    "params": {"ru": "Параметры", "en": "Params"},
    "states": {"ru": "Состояния", "en": "States"},
    "transitions": {"ru": "Переходы", "en": "Transitions"},
    "deaf": {"ru": "Не отвечает на", "en": "Does not answer"},
    "between": {"ru": "События между видами", "en": "Events between kinds"},
    "starts": {"ru": "в начале {v}", "en": "starts at {v}"},
    "masks": {"ru": "активны: {m}", "en": "active: {m}"},
    "no_masks": {"ru": "масок нет", "en": "no masks"},
    "set": {"ru": "{p} = {v}", "en": "{p} = {v}"},
    "add": {"ru": "{p} += {v}", "en": "{p} += {v}"},
    "velocity": {"ru": "скорость: вперёд {x}, вниз {y} точек/с", "en": "velocity: forward {x}, down {y} points/s"},
    "accel": {"ru": "ускорение: вперёд {x}, вниз {y} точек/с²", "en": "acceleration: forward {x}, down {y} points/s²"},
    "path": {"ru": "по формуле: {f}", "en": "by a formula: {f}"},
    "contact_on": {"ru": "ищет касания местности", "en": "looks for contacts with the terrain"},
    "contact_ev": {"ru": "ищет касания местности; коснулся — {e}", "en": "looks for contacts with the terrain; on one: {e}"},
    "contact_off": {"ru": "не ищет касаний", "en": "looks for no contacts"},
    "contact_mask": {"ru": "следит, касается ли {m} чего-то {s}{r}", "en": "watches whether {m} touches anything {s}{r}"},
    "contact_mask_on": {"ru": ": коснулась — {on}", "en": ": on touching, {on}"},
    "contact_mask_off": {"ru": "; перестала — {off}", "en": "; on letting go, {off}"},
    "contact_mask_off_only": {"ru": ": перестала — {off}", "en": ": on letting go, {off}"},
    "contact_within": {"ru": " (ближе {n} точек)", "en": " (within {n} points)"},
    "contact_stop": {"ru": "больше не следит за {m}", "en": "watches {m} no more"},
    "side_inside": {"ru": "изнутри", "en": "from inside"},
    "side_below": {"ru": "снизу", "en": "below"},
    "side_above": {"ru": "сверху", "en": "above"},
    "side_left": {"ru": "слева", "en": "on the left"},
    "side_right": {"ru": "справа", "en": "on the right"},
    "side_front": {"ru": "спереди", "en": "in front"},
    "side_back": {"ru": "сзади", "en": "behind"},
    "side_any": {"ru": "с любой стороны", "en": "on any side"},
    "side_beside": {"ru": "сбоку", "en": "beside it"},
    "carry": {"ru": "в «{p}» — кадр {pic}", "en": "at “{p}”: frame {pic}"},
    "carry_none": {"ru": "в «{p}» — своим кадром", "en": "at “{p}”: by its own frame"},
    "release": {"ru": "отпускает то, что в «{p}»: вперёд {x}, вниз {y} точек/с", "en": "lets go what is at “{p}”: forward {x}, down {y} points/s"},
    "tell": {"ru": "сообщает держащему {e}", "en": "tells the one holding it {e}"},
    "tell_cell": {"ru": "клетке карты {c} — событие {e} (значение {v})", "en": "tells the cell {c} of the map {e} (value {v})"},
    "press": {"ru": "нажимает кнопку в клетке карты {c}", "en": "presses the button in the cell {c} of the map"},
    "clear": {"ru": "убирает то, что лежит в клетке карты {c}", "en": "clears what lies in the cell {c} of the map"},
    "sense": {"ru": "ищет ближайшего: {k} в пределах {r} точек{w}; найденное — в {into}", "en": "looks for the nearest {k} within {r} points{w}; what is found goes into {into}"},
    "steer": {"ru": "правит движение: {w}; до {s} точек/с, скорость меняется не больше чем на {f} за шаг", "en": "steers: {w}; up to {s} points/s, the velocity changing by {f} at most a step"},
    "steer_seek": {"ru": "к {t}", "en": "toward {t}"},
    "steer_flee": {"ru": "прочь от {t}", "en": "away from {t}"},
    "steer_arrive": {"ru": "к {t}, замедляясь в {r} точках", "en": "toward {t}, slowing within {r} points"},
    "steer_pursue": {"ru": "наперерез: туда, где будет {t}", "en": "pursuing: to where {t} will be"},
    "steer_evade": {"ru": "прочь от того места, где будет {t}", "en": "evading where {t} will be"},
    "steer_wander": {"ru": "бродит, угол {a} сдвигается на ±{r}°", "en": "wanders, the angle {a} moving by ±{r}°"},
    "appear_with": {"ru": ", получив {p}", "en": ", given {p}"},
    "sense_any": {"ru": "кого угодно", "en": "anyone"},
    "sense_cell": {"ru": "клетку", "en": "cell"},
    "sense_where": {"ru": ", где {c}", "en": " where {c}"},
    "sense_seen": {"ru": " (только видимого: за завесой плиток не видно)", "en": " (only one in sight: the veils of tiles hide)"},
    "appear": {"ru": "появляется {k} в {x}, {y} и слышит {e}", "en": "makes {k} appear at {x}, {y}, told {e}"},
    "vanish": {"ru": "исчезает", "en": "vanishes"},
    "shoot": {"ru": "метает {k} из «{p}»: вперёд {x}, вниз {y} точек/с (из карманов, если они есть; нет — нет и броска)",
              "en": "throws {k} from “{p}”: forward {x}, down {y} points/s (out of the pockets if it has them; none there, none thrown)"},
    "draw": {"ru": "достаёт из карманов {k} в «{p}»", "en": "takes {k} out of the pockets into “{p}”"},
    "stow": {"ru": "убирает то, что в «{p}», в карманы", "en": "puts what is at “{p}” into the pockets"},
    "goto_if": {"ru": "если {c} — к {s}", "en": "if {c}: to {s}"},
    "count": {"ru": "сколько {k} в карманах", "en": "how many {k} in the pockets"},
    "count_none": {"ru": "в карманах нет {k}", "en": "there is no {k} in the pockets"},
    "count_some": {"ru": "в карманах есть {k}", "en": "there is {k} in the pockets"},
    "pockets": {"ru": "Карманы: {n}", "en": "Pockets: {n} places"},
    "stack": {"ru": "В одно место кармана — до {n}", "en": "Up to {n} in one place of a pocket"},
    "frame_x": {"ru": "кадр по выражению {e}, {ms} мс", "en": "a frame by the expression {e}, {ms} ms"},
    "amount": {"ru": "(сколько их лежит)", "en": "(how many lie)"},
    "keep": {"ru": "мир хранит, новым не уступает", "en": "the world keeps it: newer things do not take its place"},
    "hit": {"ru": "{m}, встретив {on}: {what}", "en": "{m} meeting {on}: {what}"},
    "damage": {"ru": "рана {n}", "en": "damage {n}"},
    "event": {"ru": "ему {e}", "en": "to it {e}"},
    "recoil": {"ru": "себе {e}", "en": "to itself {e}"},
    "nothing": {"ru": "ничего", "en": "nothing"},
    "signal": {"ru": "сигнал игре {n}", "en": "a signal to the game {n}"},
    "sound": {"ru": "звук {n}", "en": "sound {n}"},
    "dx": {"ru": "шаг {n} вперёд", "en": "{n} forward"},
    "dx_back": {"ru": "шаг {n} назад", "en": "{n} back"},
    "dy": {"ru": "{n} вниз", "en": "{n} down"},
    "dy_up": {"ru": "{n} вверх", "en": "{n} up"},
    "flip": {"ru": "разворот", "en": "turns round"},
    "branch": {"ru": "здесь можно прервать", "en": "may be broken off here"},
    "cycle": {"ru": "{p} по кругу {a}…{b}, кадр {ms} мс", "en": "{p} goes round {a}…{b}, a frame of {ms} ms"},
    "loop": {"ru": "по кругу", "en": "round and round"},
    "from_label": {"ru": "повторяет с «{l}»", "en": "repeats from “{l}”"},
    "to_label": {"ru": "дальше — к «{l}»", "en": "on to “{l}”"},
    "mark": {"ru": "«{l}»:", "en": "“{l}”:"},
    "then": {"ru": "дальше — {s}", "en": "then: {s}"},
    "holds": {"ru": "застывает на последнем кадре", "en": "holds its last frame"},
    "stops": {"ru": "останавливается", "en": "stops"},
    "lasts": {"ru": "{ms} мс", "en": "{ms} ms"},
    "now": {"ru": "сразу", "en": "at once"},
    "branch_when": {"ru": "в точке ветвления", "en": "at a branch point"},
    "end": {"ru": "по окончании", "en": "when it ends"},
    "rule": {"ru": "{e}: {src} → {dst}, {when}", "en": "{e}: {src} → {dst}, {when}"},
    "any_of": {"ru": "из любого ({n})", "en": "from any ({n})"},
    "sent_by": {"ru": "шлёт {who}", "en": "sent by {who}"},
    "answered": {"ru": "отвечает {who}", "en": "answered by {who}"},
    "the_game": {"ru": "игра", "en": "the game"},
    "by_game": {"ru": "Шлёт игра (своим кодом)", "en": "Sent by the game (its code)"},
    "no_answer": {"ru": "никто не отвечает", "en": "nobody answers"},
}


def _t(key, lang, **kw):
    return T[key][lang].format(**kw)


def _places(n, lang):
    if lang == "en":
        return str(n)
    if n % 10 == 1 and n % 100 != 11:
        return f"{n} место"
    if 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14:
        return f"{n} места"
    return f"{n} мест"


def _frames(n, lang):
    if lang == "en":
        return f"{n} frame" if n == 1 else f"{n} frames"
    if n % 10 == 1 and n % 100 != 11:
        return f"{n} кадр"
    if 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14:
        return f"{n} кадра"
    return f"{n} кадров"


class Kind:
    """A text of animation, read for its meaning (the assembler checks it; this only tells)."""

    def __init__(self, text):
        self.events, self.params, self.defines, self.groups = [], {}, {}, {}
        self.rules, self.scripts = [], []  # rules: (event, source, target, when); scripts: (name, [(word, rest)])
        self.pockets, self.stack = None, None
        current = None
        for raw in text.splitlines():
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            word, _, rest = line.partition(" ")
            rest = rest.strip()
            if word == "event":
                self.events += [e.strip() for e in rest.split(",") if e.strip()]
            elif word == "param":
                name, _, value = rest.partition(" ")
                self.params[name] = value.strip()
            elif word == "define":
                name, _, value = rest.partition(" ")
                self.defines[name] = value.strip()
            elif word in ("table", "game"):
                pass  # constant numbers, questions to the game: shown where expressions read them
            elif word in ("pockets", "stack") and current is None:
                setattr(self, word, rest)
            elif word == "group":
                name, _, members = rest.partition(":")
                self.groups[name.strip()] = [m.strip() for m in members.split(",") if m.strip()]
            elif word == "on":
                m = re.fullmatch(r"(\w+)\s*:\s*(\w+)\s*->\s*(\w+)\s+(now|branch|end)", rest)
                if m:
                    self.rules.append(m.groups())
            elif word == "script":
                current = (rest, [])
                self.scripts.append(current)
            elif current is not None:
                current[1].append((word, rest))


class Teller:
    def __init__(self, lang, words=None, masks=None, defines=None):
        self.lang, self.words, self.masks, self.defines = lang, words or {}, masks or {}, defines or {}
        self.labels = set()  # of the script being told: a goto to one of them names a place in it

    def label(self, name):
        return f"«{name}»" if self.lang == "ru" else f"“{name}”"

    def word(self, name):
        """A name in words, the name itself beside it when they differ."""
        w = self.words.get(name, {}).get(self.lang)
        return f"«{w}» (`{name}`)" if w and self.lang == "ru" else f"“{w}” (`{name}`)" if w else f"`{name}`"

    def plain(self, name):
        return self.words.get(name, {}).get(self.lang) or name

    def mask(self, index):
        name = self.masks.get(int(index), str(index)) if str(index).isdigit() else index
        return self.plain(name)

    def value(self, text):
        """An expression as written, its defines worked out when it is one alone."""
        text = text.strip()
        if text in self.defines:
            try:
                return f"{text} = {eval(self.defines[text], {}, {})}"
            except Exception:
                return text
        return text


def _numeric(pics):
    return all(p.isdigit() for p in pics)


def _pictures(pics, lang):
    """Frames by their pictures: one repeated, a run of numbers, or names (the first few)."""
    if len(pics) > 1 and len(set(pics)) == 1:
        return pics[0]  # one frame held: "2 frames 109"
    if _numeric(pics) and len(pics) > 1 and all(int(b) - int(a) == 1 for a, b in zip(pics, pics[1:])):
        return f"{pics[0]}–{pics[-1]}"
    shown = pics if len(pics) <= 4 else pics[:3] + ["…"]
    return ", ".join(shown)


def _steps(stmts, t):
    """The statements of a script as told items, repeats told once: (text, ms)."""
    items, i = [], 0
    while i < len(stmts):
        word, rest = stmts[i]
        # a frame worked out by an expression: told as it is written, amount in words
        if word == "frame" and rest.startswith("("):
            expr, _, ms = rest[1:].rpartition(")")
            ms = re.sub(r"\s*ms$", "", ms.strip())
            expr = re.sub(r"\bamount\b", _t("amount", t.lang), expr)
            items.append((_t("frame_x", t.lang, e=expr.strip(), ms=ms), int(ms) if ms.isdigit() else 0))
            i += 1
            continue
        # a param set to n, n+1, ... a frame each: one round
        if word == "set" and i + 1 < len(stmts) and stmts[i + 1][0] == "frame":
            p, _, v = rest.partition(" ")
            run, j, ms = [], i, None
            while j + 1 < len(stmts) and stmts[j][0] == "set" and stmts[j][1].split()[0] == p and stmts[j + 1][0] == "frame":
                vv = stmts[j][1].split(None, 1)[1].strip()
                fms = re.sub(r"ms$", "", stmts[j + 1][1].split()[1])
                if not vv.isdigit() or (run and int(vv) != run[-1] + 1) or (ms and fms != ms):
                    break
                run.append(int(vv))
                ms = fms
                j += 2
                if j < len(stmts) and stmts[j][0] == "label":
                    break  # a label begins another round: what goes round is told from it
            if len(run) >= 3:
                items.append((_t("cycle", t.lang, p=t.plain(p), a=run[0], b=run[-1], ms=ms), int(ms) * len(run)))
                i = j
                continue
        # frames of one time, one after another, with steps and sounds between them: told together
        if word == "frame":
            pics, ms, j, dx, dy, said = [], None, i, 0, 0, []
            while j < len(stmts):
                w, r = stmts[j]
                if w == "frame":
                    parts = r.split()
                    fms = re.sub(r"ms$", "", parts[1])
                    if (ms and fms != ms) or " at " in f" {r} ":
                        break
                    pics.append(parts[0])
                    ms = fms
                elif w in ("dx", "dy") and re.fullmatch(r"-?\d+", r) and j + 1 < len(stmts) and stmts[j + 1][0] in ("frame", "dx", "dy", "sound"):
                    if w == "dx":
                        dx += int(r)
                    else:
                        dy += int(r)
                elif w in ("sound", "signal") and j + 1 < len(stmts) and stmts[j + 1][0] in ("frame", "dx", "dy", "sound"):
                    said.append(_t(w, t.lang, n=r))
                else:
                    break
                j += 1
            n = len(pics)
            unit = "мс" if t.lang == "ru" else "ms"
            what = _frames(n, t.lang) + " " + _pictures(pics, t.lang) + (f", {ms} {unit}" if n == 1 else f" {'по' if t.lang == 'ru' else 'of'} {ms} {unit}")
            if dx:
                what += ", " + (_t("dx_back", t.lang, n=-dx) if dx < 0 else _t("dx", t.lang, n=dx))
            if dy:
                what += ", " + (_t("dy_up", t.lang, n=-dy) if dy < 0 else _t("dy", t.lang, n=dy))
            items.append(("; ".join([what] + said), int(ms) * n))
            i = j
            continue
        items.append((_statement(word, rest, t), 0))
        i += 1
    return [it for it in items if it[0]]


def _statement(word, rest, t):
    lang = t.lang
    args = rest.replace(",", " ").split()
    if word == "masks":
        on = [a for a in args if a != "0"] if args != ["0"] else []
        return _t("masks", lang, m=", ".join(t.mask(a) for a in on)) if on else _t("no_masks", lang)
    if word in ("set", "add"):
        p, _, v = rest.partition(" ")
        return _t(word, lang, p=t.plain(p), v=t.value(v))
    if word in ("velocity", "accel"):
        xy = [x.strip() for x in (rest.split(",") if "," in rest else rest.split())]
        return _t(word, lang, x=t.value(xy[0]), y=t.value(xy[1])) if len(xy) == 2 else ""
    if word == "path":
        return _t("path", lang, f=rest)
    if word == "contact" and args and args[0] not in ("on", "off"):
        m = t.mask(args[0])
        if args[1:] == ["off"]:
            return _t("contact_stop", lang, m=m)
        i, r = 2, ""
        if args[2:3] == ["within"]:
            r, i = _t("contact_within", lang, n=args[3]), 4
        text = _t("contact_mask", lang, m=m, s=_t("side_" + args[1], lang) if "side_" + args[1] in T else args[1], r=r)
        said = {args[k]: args[k + 1] for k in range(i, len(args) - 1, 2)}
        if "on" in said:
            text += _t("contact_mask_on", lang, on=t.word(said["on"]))
            if "off" in said:
                text += _t("contact_mask_off", lang, off=t.word(said["off"]))
        elif "off" in said:
            text += _t("contact_mask_off_only", lang, off=t.word(said["off"]))
        return text
    if word == "contact":
        if args[:1] == ["off"]:
            return _t("contact_off", lang)
        return _t("contact_ev", lang, e=t.word(args[1])) if len(args) > 1 else _t("contact_on", lang)
    if word == "carry":
        return _t("carry_none", lang, p=t.plain(args[0])) if args[1:] == ["none"] else _t("carry", lang, p=t.plain(args[0]), pic=args[1])
    if word == "release":
        x, y = (args[1], args[2]) if len(args) >= 3 else ("0", "0")
        return _t("release", lang, p=t.plain(args[0]), x=t.value(x), y=t.value(y))
    if word == "tell" and args[:1] == ["cell"]:
        parts = [p.strip() for p in " ".join(args[1:]).split(",")]
        return _t("tell_cell", lang, c=t.value(parts[0]), e=t.word(parts[1]) if len(parts) > 1 else "?",
                  v=t.value(parts[2]) if len(parts) > 2 else "0")
    if word in ("press", "clear"):
        return _t(word, lang, c=t.value(" ".join(args)))
    if word == "appear":
        head, _, given = rest.partition(" with ")
        parts = [p.strip() for p in head.split(",")]
        told = _t("appear", lang, k=parts[0], e=t.word(parts[1]) if len(parts) > 1 else "?",
                  x=t.value(parts[2]) if len(parts) > 2 else "0", y=t.value(parts[3]) if len(parts) > 3 else "0")
        return told + (_t("appear_with", lang, p=given.strip()) if given.strip() else "")
    if word == "steer":
        way, _, tail = rest.partition(" ")
        m = re.fullmatch(r"(.+?)\s+(to|from|angle)\s+(.+)", tail.strip())
        if m and way in ("seek", "flee", "arrive", "pursue", "evade", "wander"):
            nums = [x.strip() for x in m.group(1).split(",")]
            target = "(" + ", ".join(x.strip() for x in m.group(3).split(",")[:2]) + ")"
            w = _t("steer_" + way, lang, t=target, r=nums[2] if len(nums) > 2 else "", a=t.word(m.group(3).strip()))
            return _t("steer", lang, w=w, s=t.value(nums[0]), f=t.value(nums[1]) if len(nums) > 1 else "?")
    if word == "sense":
        m = re.fullmatch(r"(\w+)\s+within\s+(.+?)(\s+seen)?(?:\s+where\s+(.+?))?\s+into\s+(.+)", rest)
        if m:
            k = _t("sense_cell", lang) if m.group(1) == "cell" else _t("sense_any", lang) if m.group(1) == "any" else t.word(m.group(1))
            w = (_t("sense_seen", lang) if m.group(3) else "") + (_t("sense_where", lang, c=m.group(4)) if m.group(4) else "")
            return _t("sense", lang, k=k, r=t.value(m.group(2)), w=w, into=m.group(5))
    if word == "vanish":
        return _t("vanish", lang)
    if word == "tell":
        return _t("tell", lang, e=t.word(args[-1]))
    if word == "shoot":
        x, y = (args[2], args[3]) if len(args) >= 4 else ("0", "0")
        return _t("shoot", lang, p=t.plain(args[0]), k=t.word(args[1]), x=t.value(x), y=t.value(y))
    if word == "draw":
        return _t("draw", lang, p=t.plain(args[0]), k=t.word(args[1]))
    if word == "stow":
        return _t("stow", lang, p=t.plain(args[0]))
    if word == "keep":
        return _t("keep", lang)
    if word == "goto":
        target, _, cond = rest.partition(" if ")
        cond = re.sub(r"count\((\w+)\)\s*==\s*0\b", lambda m: _t("count_none", lang, k=t.word(m.group(1))), cond.strip())
        cond = re.sub(r"count\((\w+)\)\s*(>\s*0|>=\s*1)\b", lambda m: _t("count_some", lang, k=t.word(m.group(1))), cond)
        cond = re.sub(r"count\((\w+)\)", lambda m: _t("count", lang, k=t.word(m.group(1))), cond)
        target = target.strip()
        return _t("goto_if", lang, c=cond, s=t.label(target) if target in t.labels else t.word(target))
    if word == "hit":
        words = rest.split()
        on, i = [], 2
        while i < len(words) and words[i] not in ("damage", "event", "recoil"):
            on.append(t.mask(words[i]))
            i += 1
        what = []
        while i + 1 < len(words):
            key, v = words[i], words[i + 1]
            what.append(_t("damage", lang, n=v) if key == "damage" else _t(key, lang, e=t.word(v)))
            i += 2
        return _t("hit", lang, m=t.mask(words[0]), on=", ".join(on), what="; ".join(what) or _t("nothing", lang))
    if word in ("signal", "sound"):
        return _t(word, lang, n=rest)
    if word == "dx":
        n = int(rest) if re.fullmatch(r"-?\d+", rest) else rest
        return _t("dx_back", lang, n=-n) if isinstance(n, int) and n < 0 else _t("dx", lang, n=n)
    if word == "dy":
        n = int(rest) if re.fullmatch(r"-?\d+", rest) else rest
        return _t("dy_up", lang, n=-n) if isinstance(n, int) and n < 0 else _t("dy", lang, n=n)
    if word == "flip":
        return _t("flip", lang)
    if word == "branch":
        return _t("branch", lang)
    return ""


def _segments(stmts):
    """A script in parts, each from a label (the first from its start): [(label or None, statements)]."""
    segs = [(None, [])]
    for word, rest in stmts:
        if word == "label":
            segs.append((rest, []))
        else:
            segs[-1][1].append((word, rest))
    return segs


def _reach(stmts):
    """What of a part runs: its statements up to its first goto without if or hold (what follows is never reached), and that one."""
    for i, (word, rest) in enumerate(stmts):
        if word == "hold" or (word == "goto" and " if " not in f" {rest} "):
            return stmts[:i], (word, rest)
    return stmts, None


def _ending(name, segs, k, t, script_names):
    """How the part k of a script goes on: where its goto leads, its hold, or on into the next part or script."""
    labels = [label for label, _ in segs]
    _, stop = _reach(segs[k][1])
    if stop is None:
        if k + 1 < len(segs):
            return _t("to_label", t.lang, l=segs[k + 1][0])
        following = script_names[script_names.index(name) + 1] if name in script_names and script_names.index(name) + 1 < len(script_names) else None
        return _t("then", t.lang, s=t.word(following)) if following else _t("holds", t.lang)
    word, rest = stop
    if word == "hold":
        # a script of no frames (one that works something out) has none to hold
        framed = any(w == "frame" for _, s in segs for w, _ in s)
        return _t("holds" if framed else "stops", t.lang)
    if rest == name:
        return _t("loop", t.lang)
    if rest in labels:
        return _t("from_label" if labels.index(rest) <= k else "to_label", t.lang, l=rest)
    return _t("then", t.lang, s=t.word(rest) if rest in script_names else f"`{rest}`")


def _told(segs, k, t, mark=False):
    """A part in words: its items, the mark of its label before the first when asked; and how long its frames take."""
    items = _steps(_reach(segs[k][1])[0], t)
    texts = [text for text, _ in items]
    if mark and segs[k][0]:
        texts = [_t("mark", t.lang, l=segs[k][0]) + " " + texts[0]] + texts[1:] if texts else [_t("mark", t.lang, l=segs[k][0])]
    return texts, sum(ms for _, ms in items)


def explain(text, lang="ru", words=None, masks=None, title=None):
    """One kind in words: markdown."""
    kind = Kind(text)
    t = Teller(lang, words, masks, kind.defines)
    names = [n for n, _ in kind.scripts]
    out = [f"## {title}"] if title else []
    places = kind.pockets and (_places(int(kind.pockets), lang) if kind.pockets.isdigit() else kind.pockets)
    head = ([_t("pockets", lang, n=places)] if kind.pockets else []) + ([_t("stack", lang, n=kind.stack)] if kind.stack else [])
    if head:
        out.append(". ".join(head) + ".")
    if kind.params:
        out.append(f"**{T['params'][lang]}:** " + "; ".join(f"{t.word(p)} — {_t('starts', lang, v=v)}" for p, v in kind.params.items()))
    out.append(f"\n### {T['states'][lang]}\n")
    for name, stmts in kind.scripts:
        segs = _segments(stmts)
        t.labels = {label for label, _ in segs if label}
        # a part that ends by a goto or a hold of its own, the next one begun by a label: the script branches, told part by part
        if any(_reach(s)[1] is not None for _, s in segs[:-1]):
            texts, _ = _told(segs, 0, t)
            out.append(f"- **{t.plain(name)}**: " + "; ".join(texts + [_ending(name, segs, 0, t, names)]) + ".")
            for k in range(1, len(segs)):
                texts, _ = _told(segs, k, t)
                out.append(f"  - {_t('mark', lang, l=segs[k][0])} " + "; ".join(texts + [_ending(name, segs, k, t, names)]) + ".")
            continue
        # one way through: the parts one after another, a label marked where it begins
        texts, ms = [], 0
        for k in range(len(segs)):
            part, part_ms = _told(segs, k, t, mark=True)
            texts += part
            ms += part_ms
        ending = _ending(name, segs, len(segs) - 1, t, names)
        # what goes round has no end: its round is told (from the label on), not a length
        loops = ending in (_t("loop", lang),) or ending.startswith(_t("from_label", lang, l="").split("«")[0].split("“")[0])
        lasts = f" ({_t('lasts', lang, ms=ms)})" if ms and not loops else ""
        out.append(f"- **{t.plain(name)}**{lasts}: " + "; ".join(texts + [ending]) + ".")
    out.append(f"\n### {T['transitions'][lang]}\n")
    when = {"now": _t("now", lang), "branch": _t("branch_when", lang), "end": _t("end", lang)}
    for e in kind.events:
        for ev, src, dst, w in kind.rules:
            if ev != e:
                continue
            members = kind.groups.get(src)
            if members and len(members) > 6:
                source = _t("any_of", lang, n=len(members))
            elif members:
                source = ", ".join(t.plain(m) for m in members)
            else:
                source = t.plain(src)
            out.append("- " + _t("rule", lang, e=t.word(e), src=source, dst=t.plain(dst), when=when[w]))
    deaf = [e for e in kind.events if not any(r[0] == e for r in kind.rules)]
    if deaf:
        out.append(f"\n**{T['deaf'][lang]}:** " + ", ".join(t.word(e) for e in deaf) + ".")
    return "\n".join(out) + "\n"


def explain_pack(kinds, lang="ru", words=None, masks=None):
    """The events between kinds: who sends each, who answers it; markdown."""
    t = Teller(lang, words, masks)
    sent, answered = {}, {}
    for kname, text in kinds.items():
        kind = Kind(text)
        for sname, stmts in kind.scripts:
            for word, rest in stmts:
                parts = rest.split()
                found = []
                if word == "hit":
                    found = [(parts[i + 1], f"hit") for i in range(len(parts) - 1) if parts[i] in ("event", "recoil")]
                elif word == "tell" and parts[:1] == ["cell"]:
                    cell_parts = [x.strip() for x in rest[4:].split(",")]
                    found = [(cell_parts[1], "tell cell")] if len(cell_parts) > 1 else []
                elif word == "tell":
                    found = [(parts[-1], "tell holder")]
                elif word == "contact" and parts[:1] and parts[0] not in ("on", "off"):
                    found = [(parts[i + 1], f"contact {' '.join(parts[:2])}") for i in range(len(parts) - 1) if parts[i] in ("on", "off")]
                elif word == "contact" and len(parts) == 2:
                    found = [(parts[1], "contact on")]
                for e, how in found:
                    sent.setdefault(e, set()).add(f"{kname} ({t.plain(sname)}: {how})")
        for e, src, dst, w in kind.rules:
            answered.setdefault(e, set()).add(f"{kname}: {t.plain(src)} → {t.plain(dst)}")
    out = [f"## {T['between'][lang]}\n"]
    by_game = {}
    for e in sorted(set(sent) | set(answered)):
        who = sorted(answered.get(e, ()))
        if e not in sent:
            for a in who:  # sent by no kind: the game sends it; told shortly, by who answers
                by_game.setdefault(a.split(":", 1)[0], []).append(t.word(e))
            continue
        kinds_of = {s.split(" ", 1)[0] for s in sent[e]} | {a.split(":", 1)[0] for a in who}
        if len(kinds_of) < 2:
            continue  # within one kind
        out.append(f"- {t.word(e)}: {_t('sent_by', lang, who='; '.join(sorted(sent[e])))}; " +
                   (_t("answered", lang, who="; ".join(who)) if who else _t("no_answer", lang)) + ".")
    if by_game:
        out.append(f"\n### {T['by_game'][lang]}\n")
        for k, es in sorted(by_game.items()):
            out.append(f"- {k}: " + ", ".join(sorted(set(es))) + ".")
    return "\n".join(out) + "\n"


DIALOG_T = {
    "roles": {"ru": "Роли", "en": "Roles"},
    "game": {"ru": "игра", "en": "the game"},
    "says": {"ru": "{who}: «{text}»", "en": "{who}: “{text}”"},
    "important": {"ru": " (важно: целиком)", "en": " (important: whole)"},
    "time": {"ru": " — сам через {t}", "en": " — on by itself after {t}"},
    "chooses": {"ru": "Выбирает {who}", "en": "{who} chooses"},
    "silence": {"ru": " (молчание {t} → {label})", "en": " (silence {t} → {label})"},
    "answer": {"ru": "«{text}» → {label}", "en": "“{text}” → {label}"},
    "control": {"ru": "«{text}» — {kind}: {param}", "en": "“{text}” — {kind}: {param}"},
    "stays": {"ru": ", выбор остаётся", "en": ", the choice stays"},
    "then": {"ru": ", потом → {label}", "en": ", then → {label}"},
    "game_kind": {"ru": "**Настройки игры:** {kind}.", "en": "**The game's settings:** {kind}."},
    "if": {"ru": ", если {cond}", "en": ", if {cond}"},
    "tell": {"ru": "{who} — событие {e}", "en": "{who} is told {e}"},
    "give": {"ru": "{a} → {b}: {kind} × {n}", "en": "{a} gives {b}: {kind} × {n}"},
    "goto": {"ru": "дальше — {label}", "en": "on from {label}"},
    "end": {"ru": "конец", "en": "the end"},
    "style": {"ru": "оформление «{s}»", "en": "style “{s}”"},
    "label": {"ru": "**{l}:**", "en": "**{l}:**"},
}


def explain_dialog(text, lang="ru", words=None, strings=None):
    """
    A dialog in words: who says what, who chooses among what and where it
    leads, what follows in the world. words: names in words (roles, kinds,
    events) as for the scripts; strings: (context, msgid) -> the line in the
    language (a translation), else the msgid itself.
    """
    import re as _re
    t = Teller(lang, words)
    strings = strings or {}
    name, roles, out = None, {}, []

    def line(msgid):
        return strings.get((name, msgid), msgid)

    def who(r):
        return DIALOG_T["game"][lang] if r == "game" else t.plain(r)

    def ms(text):
        m = _re.fullmatch(r"(\d+)(ms|s)", text.strip())
        return (m.group(1) + (" с" if lang == "ru" else " s")) if m and m.group(2) == "s" else text.strip()

    for raw in text.splitlines():
        body = raw.split("#", 1)[0].rstrip() if raw.count('"') % 2 == 0 and "#" in raw and raw.index("#") > raw.rfind('"') else raw.rstrip()
        s = body.strip()
        if not s:
            continue
        word, _, rest = s.partition(" ")
        if word == "dialog":
            name = rest.strip()
            out.append(f"## {t.plain(name)}")
        elif word == "roles":
            parts = [_re.fullmatch(r'(\w+)\s+(\w+)(?:\s+"((?:[^"\\]|\\.)*)")?', p.strip()) for p in _re.findall(r'(?:[^,"]|"(?:[^"\\]|\\.)*")+', rest)]
            parts = [(m.group(1), m.group(2), m.group(3)) for m in parts if m]
            for r, k, _ in parts:
                roles[r] = k
            out.append(f"**{DIALOG_T['roles'][lang]}:** " + ", ".join(f"{t.plain(r)} ({t.word(k)}" + (f", «{line(n)}»" if n and lang == "ru" else f", “{line(n)}”" if n else "") + ")"
                                                              for r, k, n in parts) + ".")
        elif word == "game":
            out.append(DIALOG_T["game_kind"][lang].format(kind=t.word(rest.strip())))
        elif word == "label":
            out.append("\n" + DIALOG_T["label"][lang].format(l=t.plain(rest.strip())))
        elif word == "style":
            out.append("- " + DIALOG_T["style"][lang].format(s=t.plain(rest.strip())))
        elif word in ("say", "say!"):
            m = _re.match(r'(\w+)\s+"((?:[^"\\]|\\.)*)"\s*(\S*)', rest)
            if m:
                txt = DIALOG_T["says"][lang].format(who=who(m.group(1)), text=line(m.group(2)))
                if word == "say!":
                    txt += DIALOG_T["important"][lang]
                if m.group(3):
                    txt += DIALOG_T["time"][lang].format(t=ms(m.group(3)))
                out.append("- " + txt)
        elif word == "choose":
            m = _re.fullmatch(r"(\w+)(?:\s+(\S+)\s*->\s*(\w+))?", rest.strip())
            txt = DIALOG_T["chooses"][lang].format(who=who(m.group(1)))
            if m.group(2):
                txt += DIALOG_T["silence"][lang].format(t=ms(m.group(2)), label=t.plain(m.group(3)))
            out.append("- " + txt + ":")
        elif s.startswith('"'):
            m = _re.match(r'"((?:[^"\\]|\\.)*)"\s*(?:if\s+(.+?))?\s*(?:control\s+(\w+)\s+(\w+))?\s*(?:->\s*(\w+))?\s*$', s)
            if m:
                if m.group(3):
                    # a control: what it changes; with a label, on after it, else the choice stays
                    txt = DIALOG_T["control"][lang].format(text=line(m.group(1)), kind=t.word(m.group(3)), param=t.plain(m.group(4)))
                    txt += DIALOG_T["then"][lang].format(label=t.plain(m.group(5))) if m.group(5) else DIALOG_T["stays"][lang]
                else:
                    txt = DIALOG_T["answer"][lang].format(text=line(m.group(1)), label=t.plain(m.group(5) or ""))
                if m.group(2):
                    cond = _re.sub(r"count\((\w+)\)\s*>\s*0", lambda c: _t("count_some", lang, k=t.word(c.group(1))), m.group(2))
                    txt += DIALOG_T["if"][lang].format(cond=cond)
                out.append("  - " + txt)
        elif word == "tell":
            r, e = rest.split()
            out.append("- " + DIALOG_T["tell"][lang].format(who=who(r), e=t.word(e)))
        elif word == "give":
            m = _re.fullmatch(r"(\w+)\s+(\w+)\s+(\d+)\s+to\s+(\w+)", rest.strip())
            if m:
                out.append("- " + DIALOG_T["give"][lang].format(a=who(m.group(1)), b=who(m.group(4)), kind=t.word(m.group(2)), n=m.group(3)))
        elif word == "goto":
            out.append("- " + DIALOG_T["goto"][lang].format(label=t.plain(rest.strip())))
        elif word == "end":
            out.append("- " + DIALOG_T["end"][lang])
    return "\n".join(out) + "\n"


def main(argv):
    """The command line: explain the scripts of the files given (see the top of this file)."""
    import os
    lang, masks, files = "ru", {}, []
    args = list(argv)
    while args:
        a = args.pop(0)
        if a in ("-h", "--help"):
            print(__doc__.strip())
            return
        if a == "--lang" and args:
            lang = args.pop(0)
        elif a == "--masks" and args:
            masks = {int(k): v for k, v in (pair.split("=", 1) for pair in args.pop(0).split(",") if "=" in pair)}
        elif a.startswith("-"):
            raise SystemExit(f"unknown option {a}")
        else:
            files.append(a)
    if lang not in LANGS or not files:
        raise SystemExit("usage: dgx_ge_explain.py [--lang ru|en] [--masks 1=BODY,2=JAWS] script.anim ...")
    kinds = {}
    for path in files:
        with open(path, encoding="utf-8") as f:
            kinds[os.path.splitext(os.path.basename(path))[0].upper()] = f.read()
    parts = [explain_pack(kinds, lang, {}, masks)] if len(kinds) > 1 else []
    parts += [explain(text, lang, {}, masks, title=name) for name, text in kinds.items()]
    print("\n".join(parts))


if __name__ == "__main__":
    import sys
    main(sys.argv[1:])
