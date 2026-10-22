"""
The pack of hello from its art and its script: main/assets/hello.pack and its header, hello_pack.h.
Run it again after a change of art/ or anim/: python3 tools/hello_assets.py (from the example's directory, or any).
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
# the engine's tools: the engine this example lies in, or the one ESP-IDF downloaded into the project
for engine in (os.path.join(ROOT, "..", ".."), os.path.join(ROOT, "managed_components", "jef-sure__dgx-ge")):
    if os.path.exists(os.path.join(engine, "tools", "dgx_ge_pack.py")):
        sys.path.insert(0, os.path.join(engine, "tools"))
        break
from dgx_ge_pack import Pack  # noqa: E402


def art(name):
    return os.path.join(ROOT, "art", name)


pack = Pack("HELLO")

# the robot: its colors, and its pictures standing on the middle of their bottom row (they turn round about it)
bot = pack.add_set("BOT", [(90, 170, 230), (30, 60, 100), (255, 230, 110), (200, 200, 210)])
for name in ("BOT_STAND", "BOT_WALK_1", "BOT_WALK_2", "BOT_HOP"):
    pack.add_png(name, bot, art(name.lower() + ".png"), origin=(8, 19))

# the ground under it, as wide as the frame
ground = pack.add_set("GROUND", [(70, 160, 60), (40, 110, 40), (120, 80, 50), (90, 60, 40)])
pack.add_png("GROUND", ground, art("ground.png"), origin="top-left")

# the colors of the game's own: the sky (the paper of the frame) and the ink of the label
pack.add_set("UI", [(150, 205, 250), (20, 40, 80)])

# the robot's script and rules, from its own file
with open(os.path.join(ROOT, "anim", "bot.anim"), encoding="utf-8") as f:
    pack.add_anim("BOT", f.read(), path=os.path.join(ROOT, "anim", "bot.anim"))

# the sound of a hop: a short chirp going up, made here as samples (no file needed)
rate = 22050
chirp = [int(9000 * math.sin(2 * math.pi * (300 + 900 * t / (rate * 0.12)) * t / rate) * (1 - t / (rate * 0.12)))
         for t in range(int(rate * 0.12))]
pack.add_sound("HOP", samples=chirp, rate=rate)

out = os.path.join(ROOT, "main", "assets")
os.makedirs(out, exist_ok=True)
pack.write(os.path.join(out, "hello.pack"), os.path.join(out, "hello_pack.h"))
print("hello.pack:", os.path.getsize(os.path.join(out, "hello.pack")), "bytes")
