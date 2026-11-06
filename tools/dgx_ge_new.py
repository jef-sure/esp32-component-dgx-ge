#!/usr/bin/env python3
"""
A new game of DGX-GE: a project of its own in a folder anywhere, made from an example and tied to nothing around it.
It builds for the boards of jef-sure/boards and for the computer (idf.py, the board host) as it is, and is the game's
to change from there.

    python3 tools/dgx_ge_new.py <folder> [--from <example>] [--name <name>] [--engine registry|local|<folder>]

The example (--from): hello, the engine's (the default); snake, tetris or pacman, the games of the sprite editor
(dgx-ge-editor: $DGX_GE_EDITOR, else its checkout beside the engine's folder); or the folder of another game laid out
as they are, as the editor's workflow.md says a game is: its data in game_data/ (the document <name>.json, the
pictures of each kind, <KIND>.anim, dialogs/, sounds/, music/), its code in main/, the packer over the data in
tools/pack.py, its boards as board_select.py of jef-sure/boards wrote them (sdkconfig.defaults.<board>,
CMakePresets.json). The game's name (--name, else the folder's): its project, its program, its pack and the prefix of
the pack's names in C (HELLO_BOT_EVENT_HOP of hello becomes MYGAME_BOT_EVENT_HOP), its NVS namespace; a C name in lower
case, 15 letters at most (NVS's limit).

The engine (--engine): registry, the ESP Component Registry's of this engine's version; local, this checkout of the
engine (the default when this script is run from a checkout; from the registry's download, registry is); or the folder
of another checkout. main/idf_component.yml names the registry's either way; a checkout is a link to it,
components/jef-sure__dgx-ge, which ESP-IDF takes over the registry's and git ignores (a clone of the game elsewhere
takes the registry's). The link is all there is to it: made or removed later, the build follows (it looks for it).
(Not override_path: ESP-IDF names such a component after its folder, esp32-component-dgx-ge, and the component
manager's jef-sure__dgx-ge then is not found.)

The boards: the example's, its files of board_select.py as they are, with its current board. Another board, or
another current one, is board_select.py's to choose (jef-sure/boards: tools/board_select.py <board> . in the project,
board_select.py --current <board> .).

What is taken from the example: main/ (the C, main/CMakeLists.txt, main/idf_component.yml), the game's sdkconfig
defaults and its boards' (sdkconfig.defaults.<board>, CMakePresets.json), its data (game_data/, the document renamed
with the game) and its packer (tools/pack.py); what the builds leave (build/, managed_components/, sdkconfig, the
locks, the pack in main/assets/) is not. Made anew: the project's CMakeLists.txt, .gitignore, a README, .clangd and
.vscode/settings.json (clangd of ESP-IDF over the build's compile_commands.json; when ESP-IDF is here). Each place the
example is tied to where it lies is changed by a rule here; if the example no longer has that place, this stops and
says which: the example has changed and this script with it.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import textwrap

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EDITOR_GAMES = ("snake", "tetris", "pacman")
# what a pack is made of: game_data/, the folder the editor keeps (the sounds' samples and the music's notes beside the
# document)
PACK_INPUTS = ["game_data/*.json", "game_data/*/*.png", "game_data/*.anim", "game_data/dialogs/*.dlg",
               "game_data/sounds/*.wav", "game_data/music/*.mml"]
# what the builds leave and what is made anew here: never taken from the example
SKIP_DIRS = {".git", ".vscode", "__pycache__", "managed_components", "nvs", "build"}
SKIP_FILES = {"sdkconfig", "sdkconfig.old", "CMakeLists.txt", "Makefile", ".gitignore", ".clangd"}
TOUCH_FW = "main/jc8012p4a1c_touch.fw"


class Stop(Exception):
    pass


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def must_sub(pattern, repl, text, where, flags=0, count=0):
    """A place of the example changed by a rule: one match at least, else the example is not what this knows."""
    new, n = re.subn(pattern, repl, text, count=count, flags=flags)
    if not n:
        raise Stop(f"{where}: no {pattern!r} in it; the example has changed, and tools/dgx_ge_new.py must follow it")
    return new


def engine_version():
    m = re.search(r'^version:\s*["\']?([0-9][^"\'\s]*)', read(os.path.join(ENGINE, "idf_component.yml")), re.M)
    if not m:
        raise Stop(f"{ENGINE}/idf_component.yml: no version")
    return m.group(1)


def find_example(name):
    """The example's folder: hello, a game of the editor by its name, or a folder."""
    if name == "hello":
        return os.path.join(ENGINE, "examples", "hello")
    if os.path.isdir(name):
        return os.path.abspath(name)
    editors = [os.environ.get("DGX_GE_EDITOR"), os.path.join(os.path.dirname(ENGINE), "dgx-ge-editor")]
    for editor in filter(None, editors):
        game = os.path.join(editor, "examples", name)
        if os.path.isdir(os.path.join(game, "game_data")):
            return game
    raise Stop(f"--from {name}: neither hello, nor a folder, nor a game of the editor ({', '.join(EDITOR_GAMES)}) in "
               f"$DGX_GE_EDITOR/examples or {os.path.join(os.path.dirname(ENGINE), 'dgx-ge-editor', 'examples')}")


def example_files(src):
    """The example's own files, relative to it: git's (tracked, and new ones not ignored) in a checkout, else all of
    them but what the builds leave (the engine of the registry in managed_components/, which git ignores)."""
    try:
        out = subprocess.run(["git", "-C", src, "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                             capture_output=True, check=True).stdout.decode()
        files = [f for f in out.split("\0") if f]
    except (OSError, subprocess.CalledProcessError):
        files = []
    if not files:
        for root, dirs, names in os.walk(src):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith("build-")]
            files += [os.path.relpath(os.path.join(root, n), src) for n in names]
    keep = []
    for f in sorted(files):
        parts = f.split("/")
        if not os.path.isfile(os.path.join(src, f)):
            continue  # gone from the work tree, still in git's index
        if any(p in SKIP_DIRS or p.startswith("build-") for p in parts[:-1]):
            continue
        if len(parts) == 1 and (f in SKIP_FILES or f.startswith(("README", "dependencies.lock"))):
            continue
        if f.startswith("main/assets/"):
            continue  # the pack: the builds make it
        keep.append(f)
    # the 10"'s touch firmware, ignored by git (never given away), but the same person's on the same machine
    if os.path.isfile(os.path.join(src, TOUCH_FW)) and TOUCH_FW not in keep:
        keep.append(TOUCH_FW)
    return keep


def boards_of(src):
    """The example's boards, as board_select.py of jef-sure/boards wrote them into it: its files sdkconfig.defaults.<board>
    (their first line the command that made them) and CMakePresets.json; the boards, the current one first. An example
    on the engine's boards of before (dgx_ge_board.h, a table of GAME_BOARD in its CMakeLists.txt) stops this."""
    for root, _, names in os.walk(os.path.join(src, "main")):
        for n in names:
            if n.endswith((".c", ".h")) and '"dgx_ge_board.h"' in read(os.path.join(root, n)):
                raise Stop(f"{os.path.join(root, n)}: on the engine's boards of before (dgx_ge_board.h), which DGX-GE has no "
                           "more; the game moves to jef-sure/boards first (README of the engine: the boards)")
    boards = sorted(f[len("sdkconfig.defaults."):] for f in os.listdir(src)
                    if f.startswith("sdkconfig.defaults.") and read(os.path.join(src, f)).startswith("# board_select.py"))
    presets = os.path.join(src, "CMakePresets.json")
    if not boards or not os.path.isfile(presets):
        raise Stop(f"{src}: no boards of jef-sure/boards (sdkconfig.defaults.<board> and CMakePresets.json, written by its "
                   "board_select.py)")
    current = json.loads(read(presets)).get("vendor", {}).get("jef-sure/boards", {}).get("follows")
    if current in boards:
        boards.remove(current)
        boards.insert(0, current)
    return boards


def rename(text, old, new):
    """The game's name in its files: the prefix of the pack's names in C, the pack and its header, the document of the
    editor, the name as a string (the log's tag, the NVS namespace, the packer's prefix)."""
    if old == new:
        return text
    text = re.sub(r"\b%s_" % old.upper(), new.upper() + "_", text)
    for a, b in ((old + "_pack", new + "_pack"), (old + ".pack", new + ".pack"), (old + ".json", new + ".json"),
                 ('"%s"' % old, '"%s"' % new), ('"%s"' % old.upper(), '"%s"' % new.upper())):
        text = text.replace(a, b)
    return text


def engine_entry(version):
    return (f'  jef-sure/dgx-ge:\n    version: "^{version}"\n'
            "    # the registry's; a checkout instead: a link to it, components/jef-sure__dgx-ge (README)\n")


def manifest(text, entry, where):
    """main/idf_component.yml: the engine's entry (with whatever was under it, override_path, and the comments over it,
    which tell of the example's place) replaced by the project's."""
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        if re.match(r"  jef-sure/dgx-ge:", line):
            j = i + 1
            while j < len(lines) and (lines[j].startswith("    ") or not lines[j].strip()):
                j += 1
            while i and re.match(r"  #", lines[i - 1]):
                i -= 1
            return "".join(lines[:i]) + entry + "".join(lines[j:])
    raise Stop(f"{where}: no entry jef-sure/dgx-ge; the example has changed, and tools/dgx_ge_new.py must follow it")


def main_cmake(text, old, new):
    where = "main/CMakeLists.txt"
    if "idf_build_get_property(engine GAME_ENGINE)" not in text:
        raise Stop(f"{where}: no idf_build_get_property(engine GAME_ENGINE); the example has changed, and "
                   "tools/dgx_ge_new.py must follow it")
    # the music's notes are inputs of the pack too
    if "game_data/music/" not in text:
        text = text.replace("${game}/game_data/sounds/*.wav", "${game}/game_data/sounds/*.wav ${game}/game_data/music/*.mml")
    return rename(text, old, new)


def top_cmake(name, boards):
    host = "host" in boards
    lines = [f"# {name} for a board of jef-sure/boards, or for the computer. The boards are chosen once, by tools/board_select.py",
             "# of jef-sure/boards, which wrote sdkconfig.defaults.<board> here and CMakePresets.json, whose preset builds a",
             "# board in build/<board> (ESP-IDF 6 reads the presets; ESP-IDF 5: idf.py -B build/<board>",
             "# -D SDKCONFIG=build/<board>/sdkconfig -D BOARD=<board> build):",
             f"#   idf.py build                            the current board, {boards[0]}"]
    lines += [f"#   idf.py --preset {b} build" for b in boards[1:] if b != "host"]
    if host:
        lines.append(f"#   idf.py --preset host build              the computer: build/host/{name}.elf")
    lines += ["# Its sdkconfig: the board's settings, then the game's own (sdkconfig.defaults), a later file over an earlier.",
              "cmake_minimum_required(VERSION 3.22)",
              "",
              "# BOARD=<board>, which the preset of a board sets, builds with sdkconfig.defaults.<board> that board_select.py",
              "# wrote, then with the game's own sdkconfig.defaults. A -D SDKCONFIG_DEFAULTS=... on the command line wins.",
              'set(BOARD "" CACHE STRING "a board of this project: its file sdkconfig.defaults.<board>")',
              "if(NOT SDKCONFIG_DEFAULTS AND BOARD)",
              "\tif(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.${BOARD})",
              '\t\tmessage(FATAL_ERROR "no sdkconfig.defaults.${BOARD} here: run board_select.py ${BOARD} . of jef-sure/boards")',
              "\tendif()",
              '\tset(SDKCONFIG_DEFAULTS "sdkconfig.defaults.${BOARD};sdkconfig.defaults")',
              "endif()",
              "",
              "# The engine, for main/CMakeLists.txt (its touch, its packer): a checkout linked as components/jef-sure__dgx-ge,",
              "# which ESP-IDF takes over the registry's (README), else the registry's, downloaded into managed_components/",
              "set(engine ${CMAKE_CURRENT_LIST_DIR}/components/jef-sure__dgx-ge)",
              "if(NOT EXISTS ${engine}/dgx_ge_di.cmake)",
              "\tset(engine ${CMAKE_CURRENT_LIST_DIR}/managed_components/jef-sure__dgx-ge)",
              "endif()",
              "",
              "# the computer (BOARD=host, the linux target of ESP-IDF): main and what it requires, the drivers of a chip are not",
              "# there",
              'if(BOARD STREQUAL "host")',
              "\tset(COMPONENTS main)",
              "\t# in a shell of ESP-IDF the linker of a chip comes first in PATH: the compiler of the computer looks in /usr/bin first",
              '\tset(CMAKE_EXE_LINKER_FLAGS "-B/usr/bin/")',
              "endif()",
              "",
              "include($ENV{IDF_PATH}/tools/cmake/project.cmake)",
              "# each board has its lock of the components beside the project: dependencies.lock.<board>",
              "if(BOARD)",
              "\tidf_build_set_property(DEPENDENCIES_LOCK ${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${BOARD})",
              "endif()",
              "# the engine for main/CMakeLists.txt: a property of the build, which ESP-IDF hands on even where it reads that",
              "# file early",
              "idf_build_set_property(GAME_ENGINE ${engine})",
              f"project({name})",
              ""]
    return "\n".join(lines)


def editor_settings(dst, board):
    """VS Code's: the ESP-IDF extension's setup and ESP's clangd over the compile_commands.json of the current board's
    build (paths of this machine, so git ignores them); None without ESP-IDF here."""
    import glob
    idf = os.environ.get("IDF_PATH") or next(iter(sorted(glob.glob(os.path.expanduser("~/.espressif/v*/esp-idf")))[-1:]), None)
    clangd = sorted(glob.glob(os.path.expanduser("~/.espressif/tools/esp-clangd/*/esp-clangd/bin/clangd")))
    if not idf and not clangd:
        return None
    settings = {"idf.currentSetup": idf} if idf else {}
    if clangd:
        settings["clangd.path"] = clangd[-1]
        settings["clangd.arguments"] = ["--background-index", "--query-driver=**",
                                        f"--compile-commands-dir={dst}/build/{board}"]
    return json.dumps(settings, indent=4) + "\n"


CLANGD = """\
# clangd over ESP-IDF's compile_commands.json (build/<board>/): the compiler's own flags of the chip left out, which
# clangd does not know
CompileFlags:
    Remove: [-f*, -m*]
"""


def gitignore(has_fw_board):
    lines = ["# made by the builds, each board in build/<board> (the locks of the components too: they record where a "
             "checkout of the",
             "# engine lies)"]
    lines += ["build/", "managed_components/", "sdkconfig", "sdkconfig.old", "main/assets/", "dependencies.lock*",
              "# a checkout of the engine on this machine, in place of the registry's (README)",
              "components/jef-sure__dgx-ge",
              "# VS Code's settings: paths of this machine",
              ".vscode/"]
    if has_fw_board:
        lines += ["# the 10\"'s touch panel's firmware: the game's own, never given away (README)", TOUCH_FW]
    return "\n".join(lines) + "\n"


def readme(name, src_label, version, mode, engine_rel, boards):
    current = boards[0]
    board_cmds = [f"idf.py build flash monitor                  # the current board, {current}"]
    board_cmds += [f"idf.py --preset {b} build flash monitor" for b in boards[1:] if b != "host"]
    what = [("`main/main.c`", "what the data cannot say"),
            ("`game_data/`", f"the data: the folder the sprite editor ([dgx-ge-editor](https://github.com/jef-sure/dgx-ge-editor)) "
             f"keeps, `{name}.json`, the pictures, `*.anim`, `dialogs/`, `sounds/`, `music/`"),
            ("`tools/pack.py`", f"the pack from `game_data/`: `main/assets/{name}.pack` and `{name}_pack.h` (the build runs it)"),
            ("`CMakeLists.txt`, `main/CMakeLists.txt`, `main/idf_component.yml`", "the build, for a board and for the computer"),
            ("`sdkconfig.defaults.<board>`, `CMakePresets.json`", "the boards, as `tools/board_select.py` of "
             "[jef-sure/boards](https://github.com/jef-sure/esp32-component-boards) wrote them: its to change"),
            ("`sdkconfig.defaults`", "the game's own settings of ESP-IDF, after the board's"),
            ("`.clangd`, `.vscode/settings.json`", f"clangd over the build's `build/{current}/compile_commands.json`, the "
             "ESP-IDF extension's setup (this machine's: git ignores `.vscode/`)")]
    wrap = lambda text: "\n".join(textwrap.wrap(text, width=118, break_on_hyphens=False))  # noqa: E731
    if mode == "registry":
        now = "Now it is the registry's, downloaded into `managed_components/` with jef-sure/boards and what the board needs."
    else:
        now = (f"Now it is the checkout `{engine_rel}`, linked. ESP-IDF warns that the component's name, jef-sure/dgx-ge, "
               "is not its folder's (the checkout's): harmless.")
    engine_text = "\n\n".join(t if t.startswith("```") else wrap(t) for t in (
        f"`main/idf_component.yml` names the ESP Component Registry's, `jef-sure/dgx-ge` `^{version}`. A checkout of "
        "the engine instead (to change the engine together with the game) is a link to it, `components/jef-sure__dgx-ge`: "
        "ESP-IDF takes a component of the project over the registry's, and the build looks for the link first "
        "(`CMakeLists.txt`). Git ignores the link, so the game cloned elsewhere takes the registry's.",
        "```sh\nln -s ../../esp32-component-dgx-ge components/jef-sure__dgx-ge    # a checkout (the path from components/)"
        "\nrm components/jef-sure__dgx-ge                                     # the registry's"
        "\nidf.py reconfigure                                                 # either way, then\n```",
        now))
    fw = ""
    if "jc8012p4a1c" in boards:
        fw = "\n" + wrap("The Guition JC8012P4A1C (10\") needs its touch panel's firmware, which may not be given away: "
                         "make `main/jc8012p4a1c_touch.fw` from the board's package with DGX-DI's "
                         "`tools/gsl_firmware.py` (jef-sure/boards: `docs/en/touch-firmware.md`).") + "\n"
    host = ""
    if "host" in boards:
        host = f"""
## On the computer

The board `host` of jef-sure/boards: the linux target of ESP-IDF, the display a window of DGX as large as the board
it shows (`sdkconfig.defaults.host`), the mouse its finger, the keys its buttons, the sound the computer's, NVS in
`build/host/flash.bin`. Needs SDL2 (`libsdl2-dev`). With nobody at it: `DGX_HOST_SCRIPT`, `DGX_HOST_QUIT_MS`,
`DGX_HOST_SHOT` (the engine's `docs/host-en.md`, section 7).

```sh
idf.py --preset host build
./build/host/{name}.elf
```
"""
    rows = "\n".join(f"| {a} | {b} |" for a, b in what)
    return f"""# {name}

A game on [DGX-GE](https://github.com/jef-sure/esp32-component-dgx-ge), made by `tools/dgx_ge_new.py` of DGX-GE
{version} from {src_label}. The same `main/main.c` builds for every board of
[jef-sure/boards](https://github.com/jef-sure/esp32-component-boards) and for the computer.

## What is where

| File | What it is |
| --- | --- |
{rows}

## On a board

ESP-IDF 5.5 or later (6 reads the presets), in a shell with it activated; the pack wants the system's python3 with
Pillow.

```sh
{chr(10).join(board_cmds)}
```

Another board: `python3 managed_components/jef-sure__boards/tools/board_select.py <board> .` (after a first build
downloaded it), then `idf.py --preset <board> build`.
{fw}{host}
## The engine

{engine_text}

## The pack

The build makes it whenever what it is made of changes, as it configures. By hand: `python3 tools/pack.py <the
engine's folder>`.
"""


def make_project(args):
    version = engine_version()
    src = find_example(args.source)
    dst = os.path.abspath(args.folder)
    if os.path.exists(dst) and (not os.path.isdir(dst) or os.listdir(dst)):
        raise Stop(f"{dst}: there is something there already; a new game wants a new or empty folder")
    name = args.name or os.path.basename(dst.rstrip("/")).lower().replace("-", "_")
    if not re.fullmatch(r"[a-z][a-z0-9_]{0,14}", name):
        raise Stop(f"the name {name!r}: a C name in lower case, 15 letters at most (an NVS namespace); --name <name>")
    if name.startswith(("dgx", "esp", "idf", "config", "board")):
        raise Stop(f"the name {name!r}: its prefix in C, {name.upper()}_, would meet the libraries' names; another one")

    top = read(os.path.join(src, "CMakeLists.txt"))
    m = re.search(r"^project\((\w+)\)", top, re.M)
    if not m:
        raise Stop(f"{src}/CMakeLists.txt: no project(<name>)")
    old = m.group(1)
    if not os.path.isfile(os.path.join(src, "game_data", old + ".json")):
        raise Stop(f"{src}: no game_data/{old}.json, the document of the editor named as the project: not a game laid "
                   "out as the editor's workflow.md says")
    boards = boards_of(src)

    if args.engine is None:  # run from a checkout of the engine: that checkout, linked; from a download: the registry's
        args.engine = "local" if os.path.exists(os.path.join(ENGINE, ".git")) else "registry"
    if args.engine == "registry":
        mode, engine_dir = "registry", None
    else:
        mode = "local"
        engine_dir = ENGINE if args.engine == "local" else os.path.abspath(args.engine)
        if not os.path.isfile(os.path.join(engine_dir, "dgx_ge_di.cmake")):
            raise Stop(f"--engine {args.engine}: no DGX-GE at {engine_dir}")

    def rel(p, base):
        """A checkout as a path from the project: relative beside it (the checkouts side by side), else absolute."""
        r = os.path.relpath(p, base).replace(os.sep, "/")
        return r if r.split("/").count("..") <= 3 and os.path.commonpath([p, base]) != os.sep else p.replace(os.sep, "/")
    engine_rel = rel(engine_dir, dst) if engine_dir else None

    made = {}
    for f in example_files(src):
        path = os.path.join(src, f)
        if f == "main/CMakeLists.txt":
            made[f] = main_cmake(read(path), old, name)
        elif f == "main/idf_component.yml":
            made[f] = manifest(read(path), engine_entry(version), f)
        elif f == "tools/pack.py":
            made[f] = rename(read(path), old, name)
        elif f == f"game_data/{old}.json":
            text = read(path)
            if old != name:
                text = must_sub(r'^(\s*"name":\s*)"%s"' % re.escape(old), r'\1"%s"' % name, text, f, flags=re.M, count=1)
                if json.loads(text).get("name") != name:
                    raise Stop(f"{f}: its name did not become {name!r}")
            made[f"game_data/{name}.json"] = text
        elif f.startswith("main/") and f.endswith((".c", ".h")):
            made[f] = rename(read(path), old, name)
        else:
            made[f] = None  # as it is: the boards' files of board_select.py among them
    for need in ("main/CMakeLists.txt", "main/idf_component.yml", "main/main.c", "tools/pack.py", "CMakePresets.json"):
        if need not in made:
            raise Stop(f"{src}: no {need if need != 'tools/pack.py' else 'script of its pack'}")
    if "jef-sure/boards" not in (made["main/idf_component.yml"] or ""):
        raise Stop(f"{src}/main/idf_component.yml: no jef-sure/boards; the example has changed, and tools/dgx_ge_new.py "
                   "must follow it")

    has_fw_board = "jc8012p4a1c" in boards
    os.makedirs(dst, exist_ok=True)
    for f, text in made.items():
        out = os.path.join(dst, f)
        if text is None:
            os.makedirs(os.path.dirname(out), exist_ok=True)
            shutil.copy2(os.path.join(src, f), out)
        else:
            write(out, text)
    if os.path.isfile(os.path.join(dst, "tools", "pack.py")):
        os.chmod(os.path.join(dst, "tools", "pack.py"), 0o755)
    if engine_dir:
        os.makedirs(os.path.join(dst, "components"), exist_ok=True)
        os.symlink(rel(engine_dir, os.path.join(dst, "components")), os.path.join(dst, "components", "jef-sure__dgx-ge"))
    write(os.path.join(dst, "CMakeLists.txt"), top_cmake(name, boards))
    write(os.path.join(dst, ".clangd"), CLANGD)
    settings = editor_settings(dst, boards[0])
    if settings:
        write(os.path.join(dst, ".vscode", "settings.json"), settings)
    write(os.path.join(dst, ".gitignore"), gitignore(has_fw_board))
    if os.path.realpath(src) == os.path.realpath(os.path.join(ENGINE, "examples", "hello")):
        src_label = "the example hello"
    elif os.path.basename(os.path.dirname(src)) == "examples":
        src_label = f"the editor's example {old}"
    else:
        src_label = f"the game {old}"
    write(os.path.join(dst, "README.md"), readme(name, src_label, version, mode, engine_rel, boards))

    print(f"{dst}: {name}, from {src_label} ({src}); the engine: "
          + (f"the registry's, ^{version}" if mode == "registry" else f"{engine_dir}, as components/jef-sure__dgx-ge"))
    print(f"  boards: {', '.join(boards)} (the current: {boards[0]})")
    if TOUCH_FW in made:
        print(f"  {TOUCH_FW}: taken from the example (ignored by git)")
    print("Then, in a shell with ESP-IDF:")
    print(f"  cd {os.path.relpath(dst) if not os.path.relpath(dst).startswith('../..') else dst}")
    print(f"  idf.py build                 # the firmware of {boards[0]}; the engine, the boards and what they need downloaded")
    if "host" in boards:
        print(f"  idf.py --preset host build && ./build/host/{name}.elf     # the game on the computer")


def main(argv):
    ap = argparse.ArgumentParser(description="A new game of DGX-GE in a folder of its own, made from an example.")
    ap.add_argument("folder", help="the project's folder: new, or empty")
    ap.add_argument("--from", dest="source", default="hello", metavar="EXAMPLE",
                    help=f"hello (the default), {', '.join(EDITOR_GAMES)} (the editor's), or a game's folder")
    ap.add_argument("--name", help="the game's name: a C name in lower case, 15 letters at most (else the folder's)")
    ap.add_argument("--engine", metavar="registry|local|FOLDER",
                    help="the registry's engine of this version, this checkout (the default when this is run from a "
                         "checkout), or another checkout")
    args = ap.parse_args(argv)
    try:
        make_project(args)
    except Stop as e:
        sys.exit(f"dgx_ge_new: {e}")


if __name__ == "__main__":
    main(sys.argv[1:])
