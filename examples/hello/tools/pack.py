#!/usr/bin/env python3
"""
The pack of the game from game_data/, the folder the sprite editor keeps (its document, its pictures by kind,
<KIND>.anim, dialogs/*.dlg), by the engine's packer: main/assets/<name>.pack and its header <name>_pack.h. The builds
call it (make and idf.py alike), nobody needs to: python3 tools/pack.py <engine> [<the game's folder>]

The packer takes what lies beside the document as well: the sounds' samples, sounds/<name>.wav, and the music's notes,
music/<name>.mml (the editor keeps their names). The constants of the document reach the C in the pack's header.
"""
import glob
import os
import sys

sys.dont_write_bytecode = True
engine = sys.argv[1]
game = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
data = os.path.join(game, "game_data")
sys.path.insert(0, os.path.join(engine, "tools"))
from dgx_ge_pack import Pack  # noqa: E402

docs = glob.glob(os.path.join(data, "*.json"))
if len(docs) != 1:
    sys.exit(f"{data}: one document .json of the sprite editor is wanted at the top, there are {len(docs)}")
name = os.path.splitext(os.path.basename(docs[0]))[0]
out = os.path.join(game, "main", "assets")
os.makedirs(out, exist_ok=True)
pack = Pack(name.upper())
pack.add_sprites(docs[0])
pack.write(os.path.join(out, name + ".pack"), os.path.join(out, name + "_pack.h"))
print(f"{name}.pack: {os.path.getsize(os.path.join(out, name + '.pack'))} bytes")
