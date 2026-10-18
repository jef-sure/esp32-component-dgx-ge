"""
The graph of scenes of DGX-GE (see include/dgx_ge_graph.h), made by the game's
tools and put into a pack (Pack.add_graph):

    g = Graph("WORLD")
    g.map("level1", data="LEVEL_1", rules=\"\"\"
    event death, end
    param deaths 0
    script rest
        hold
    script died
        add deaths 1
        go start            # an exit of the map: its signal
    on death: rest -> died now
    \"\"\")
    a = g.scene("level1", 1)            # a scene of a map, by its number there
    b = g.scene("level1", 2)
    g.side(a, "right", b)               # one way: and back is another
    g.exit(a, "door", b, "start", "fade")     # by a name, to an entry, by a transition
    g.exit("level1", "start", a, "start")     # of the whole map: its rules take it
    g.entry(b, "start", 40, 20, left=True, arrive="falls")
    g.start(a, "start")

Rules are scripts of an animation (dgx_ge_anim.py) whose events are the map's;
"go <exit>" is a signal of the exit's name. Names of exits and entries are
numbers of the graph: <PREFIX>_<GRAPH>_EXIT_<name>, _ENTRY_<name>, _MAP_<name>.
"""

import re
import struct

MAGIC = 0x47534744  # "DGSG"
VERSION = 3
SIDES = {"left": 0, "right": 1, "up": 2, "down": 3}
TRANSITIONS = {"cut": 0, "fade": 1, "wipe": 2, "seamless": 3}  # seamless: at once and as one is
KINDS = {"room": 0, "show": 1}  # a scene of a map of data (a room of a level), a show (its map: the show)
NONE = 0xFFFF


class Graph:
    def __init__(self, name):
        self.name = name
        self.maps = []        # (name, data blob name, rules text, the number it is named by)
        self.scenes = []      # [map index, number in the map, kind, sides, event of coming in, events of leaving by the sides]
        self.exits = []       # (from, of map, to, name, entry, transition)
        self.entries = []     # (scene, name, x, y, left, arrive event name, the first link it fires)
        self.exit_names, self.entry_names = [], []
        self.begin = None

    def _name(self, names, name):
        if name not in names:
            names.append(name)
        if len(names) > 255:
            raise ValueError(f"graph {self.name}: 255 names at most")
        return names.index(name)

    def map(self, name, data=None, rules=None, number=0):
        """
        A map (a cloud of scenes that share data): the blob of its data, the text of its rules (None: none), the number
        the game names it by (0 none).
        """
        if any(m[0] == name for m in self.maps):
            raise ValueError(f"graph {self.name}: map {name} twice")
        if not 0 <= number <= 0xFFFF:
            raise ValueError(f"graph {self.name}: map {name} numbered {number}")
        self.maps.append((name, data, rules, number))
        return len(self.maps) - 1

    def _map_index(self, name):
        for i, m in enumerate(self.maps):
            if m[0] == name:
                return i
        raise ValueError(f"graph {self.name}: no map {name}")

    def scene(self, map_name, index, kind=0, on_enter=None):
        """
        A scene of a map, by its number there, of a kind ("room", "show": a cut scene, its map's data a show,
        dgx_ge_show.h); on_enter: an event its map's rules are told when one comes in. Its number.
        """
        if not 0 <= index <= 255:
            raise ValueError(f"graph {self.name}: a scene numbered {index}")
        self.scenes.append([self._map_index(map_name), index, KINDS.get(kind, kind), [NONE] * 4, on_enter, [None] * 4])
        return len(self.scenes) - 1

    def side(self, a, side, b):
        """From scene a by a side to scene b (one way)."""
        self.scenes[a][3][SIDES[side]] = b

    def leave(self, a, side, event):
        """The event a's map's rules are told as one leaves scene a by a side (with no scene beside: an edge out)."""
        self.scenes[a][5][SIDES[side]] = event

    def exit(self, frm, name, to, entry, transition="cut"):
        """An exit by a name: of a scene (a number), or of a map (its name), to a scene, its entry, by a transition."""
        of_map = isinstance(frm, str)
        self.exits.append((self._map_index(frm) if of_map else frm, of_map, to, self._name(self.exit_names, name),
                           self._name(self.entry_names, entry), TRANSITIONS[transition]))

    def entry(self, scene, name, x, y, left=False, arrive=None, fires=None):
        """
        An entry of a scene: where one comes in, facing which way, told an event (arrive: a name of the pack's events),
        firing links of the map as one comes (fires: the first of them, dgx_ge_links.h).
        """
        if fires is not None and not 0 <= fires < 255:
            raise ValueError(f"graph {self.name}: an entry fires link {fires}")
        self.entries.append((scene, self._name(self.entry_names, name), x, y, left, arrive, fires))

    def start(self, scene, entry):
        self.begin = (scene, self._name(self.entry_names, entry))

    def rules_text(self, text):
        """Rules with go <exit> as signals of the names of exits."""
        return re.sub(r"^(\s*)go\s+(\w+)\s*$", lambda m: f"{m.group(1)}signal {self._name(self.exit_names, m.group(2))}", text, flags=re.M)

    def blob(self, blob_of, event_of):
        """The blob; blob_of: a name of the pack's blobs to its number (None: 0xFFFF), event_of: an event's name to its number."""
        for frm, of_map, to, *_ in self.exits:
            if not 0 <= to < len(self.scenes) or (not of_map and not 0 <= frm < len(self.scenes)):
                raise ValueError(f"graph {self.name}: an exit of no scene")
        if self.begin is None:
            raise ValueError(f"graph {self.name}: no start")
        out = struct.pack("<IHHHHBBHBB", MAGIC, VERSION, len(self.scenes), len(self.exits), len(self.entries), len(self.maps), 0,
                          self.begin[0], self.begin[1], 0)
        for name, data, rules, number in self.maps:
            out += struct.pack("<HHH", blob_of(data), blob_of(f"{self.name}_{name}_RULES" if rules else None), number)
        for m, index, kind, sides, on_enter, leaves in self.scenes:
            out += struct.pack("<BBBB4H", m, index, kind, 0xFF if on_enter is None else event_of(on_enter), *sides)
            out += bytes(0xFF if e is None else event_of(e) for e in leaves)
        for frm, of_map, to, name, entry, transition in self.exits:
            out += struct.pack("<HHBBBB", frm, to, name, entry, transition, 1 if of_map else 0)
        for scene, name, x, y, left, arrive, fires in self.entries:
            out += struct.pack("<HhhBBBB", scene, x, y, name, 1 if left else 0, 0xFF if arrive is None else event_of(arrive),
                               0xFF if fires is None else fires)
        return out
