"""
The pack of the music's test (test_music.c): musics of one note each to measure the synthesizer by (a square, the
envelope, noise, the clock at a tempo), ones that go round or end, a sound for the effects beside them, and a script
that plays a music and waits for it to end. Run: make_music_pack.py <out dir>.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from dgx_ge_pack import Pack  # noqa: E402

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
pack = Pack("MU")
pack.set_music_synth("chip")  # these songs are of the chip (the default is fm)
pack.add_set("ONE", [(255, 255, 255)])
pack.add_image("DOT", 0, 1, 1, [1], 1)
# the script names its music before the music has notes: the number is the name's, the notes come after
pack.add_anim("PLAYER", """
script go
    music theme
label waiting
    frame DOT 10ms
    goto waiting if music >= 0
    signal 1
    music stop
    hold
script quiet
    music stop
    hold
script change
    music noise 100ms
    hold
script dim
    music volume 50% 100ms
    hold
""")
# A4, 440 Hz, a square of 25%, a whole note at 120: two seconds, no envelope
pack.add_music("square", "@1 = square 25%\nA t120 o4 @1 a1\n")
# the envelope: attack 10 ms, decay 20 to a sustain of 5, release 30, the gate half the note (a half note at 120: 1 s)
pack.add_music("envelope", "@1 = square attack 10 decay 20 sustain 5 release 30\nA t120 o0 @1 q4 c2\n")
# noise at o4 c
pack.add_music("noise", "@2 = noise\nA o4 @2 c4\n")
# the clock: a quarter note at 150, a rest; a quarter at 144 (not a whole number of samples at 22050)
pack.add_music("clock", "A t150 o4 c4 r4\n")
pack.add_music("odd", "A t144 o4 c4\n")
# the tempo changing, a tie: c4 at 120, c4 at 240 (half as long), c4&c4 at 240 one note of two quarters
pack.add_music("tempo", "A t120 o4 c4 r4 t240 c4 r4 c4&c4 r4\n")
# going round from $: forever; two channels, one ending, one going round
pack.add_music("round", "A o4 c8 $ e8 g8\nB o2 c2\n")
pack.add_music("theme", "A t240 o5 l16 c e g > c\n")
pack.add_sound("click", samples=[1000, 2000, 3000, 4000] * 50, rate=8000, codec="pcm8")
pack.write(os.path.join(out, "music.pack"), os.path.join(out, "music_pack.h"), os.path.join(out, "music.sounds"))
