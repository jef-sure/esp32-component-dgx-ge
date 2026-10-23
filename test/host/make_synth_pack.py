"""
The sounds of the synthesizers' test (test_synth.c): songs of one note or a few on FM (a sine, a modulated carrier
with feedback, the envelope, the waves, the additive connection, a percussive envelope, the key scale level), on the
NES (the pulse's four duties, the triangle, the noise's two modes, the volume's 16 levels, a note below the timer's
reach), a chip song with a tail, the project's synthesizer and a song's own, a set's instrument. Run:
make_synth_pack.py <out dir>.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from dgx_ge_pack import Pack  # noqa: E402

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
pack = Pack("SY")
pack.set_music_synth("chip")  # songs without #synth here are of the chip (the default is fm)
pack.add_set("ONE", [(255, 255, 255)])
pack.add_image("DOT", 0, 1, 1, [1], 1)
# FM: the carrier alone (no mod(): the modulator silent), A4 a whole note at 120: two seconds
pack.add_music("fm_sine", "#synth fm\n@1 = fm car(mult 1)\nA t120 o4 @1 a1\n")
# the modulator (twice the note, 7.5 dB down) into the carrier, its feedback 5
pack.add_music("fm_mod", "#synth fm\n@1 = fm mod(mult 2 level 10 feedback 5) car(mult 1)\nA t120 o4 @1 a1\n")
# the envelope: attack 10 ms, decay (96 dB) 100 ms to 9 dB down, the gate half the note (a half: 1 s), release 50 ms
pack.add_music("fm_env", "#synth fm\n@1 = fm car(attack 10 decay 100 sustain 12 release 50)\nA t120 o4 @1 q4 a2\n")
# the waves: half-sine, abs-sine, quarter-sine, a quarter each
pack.add_music("fm_waves", "#synth fm\n@1 = fm car(wave half-sine)\n@2 = fm car(wave abs-sine)\n@3 = fm car(wave quarter-sine)\nA t120 o4 @1 a4 @2 a4 @3 a4\n")
# both heard: the modulator (twice the note) beside the carrier
pack.add_music("fm_add", "#synth fm\n@1 = fm mod(mult 2) car(mult 1) connect add\nA t120 o4 @1 a1\n")
# percussive: past its sustain (9 dB down) it goes on down at its release's pace (500 ms a 96 dB) while the note sounds
pack.add_music("fm_perc", "#synth fm\n@1 = fm car(decay 100 sustain 12 release 500 percussive)\nA t120 o4 @1 a1\n")
# the key scale level: 6 dB an octave from G0; o2 a and o6 a, four octaves apart
pack.add_music("fm_ksl", "#synth fm\n@1 = fm car(ksl 6)\nA t120 @1 o2 a4 o6 a4\n")
# the lowest c (16.35 Hz): every place of the quarter of a sine in turn
pack.add_music("fm_low", "#synth fm\n@1 = fm car()\nA t120 o0 @1 c4\n")
# the release alone: the note over at 0.5 s, then 50 ms a 96 dB down to where nothing is heard, and the song is over
pack.add_music("fm_rel", "#synth fm\n@1 = fm car(release 50)\nA t120 o4 @1 a4\n")
# the tremolo (1 dB at 3.7 Hz) and the vibrato (7 cents at 6.1 Hz)
pack.add_music("fm_lfo", "#synth fm\n@1 = fm car(vibrato tremolo)\nA t120 o4 @1 a1\n")
# NES: the pulse of 12.5, 25, 50 and 75%, a quarter each
pack.add_music("nes_pulse", "#synth nes\n@1 = square 12.5%\n@2 = square 25%\n@3 = square 50%\n@4 = square 75%\nA t120 o4 @1 a4 @2 a4 @3 a4 @4 a4\n")
# the triangle: no volume of its own (v8 says nothing to it)
pack.add_music("nes_tri", "#synth nes\n@1 = triangle\nA t120 o4 @1 v8 a4\n")
# the noise, long then short
pack.add_music("nes_noise", "#synth nes\n@1 = noise long\n@2 = noise short\nA t120 o4 @1 c4 @2 c4\n")
# the volume: v8 of the 16 levels; a note lower than the pulse's timer reaches (o1 c: 32.7 Hz), an octave up
pack.add_music("nes_volume", "#synth nes\nA t120 o4 v8 a4 o1 c4\n")
# the chip with a tail: its notes over, 100 ms more, then cut (its release of 1 s would ring on)
pack.add_music("chip_tail", "#tail 100ms\n@1 = square release 1000\nA t120 o4 @1 a4\n")
# the project's synthesizer the NES: a song without #synth on it; one with its own, the chip
pack.set_music_synth("nes")
pack.add_music("project_nes", "@1 = square 75%\nA t120 o4 @1 a4\n")
pack.add_music("own_chip", "#synth chip\nA t120 o4 a4\n")
pack.set_music_synth("chip")
# a set's instrument by its name
pack.add_music("set_piano", "#synth fm\n#set msx-music\nA t120 o4 @piano a4\n")
pack.write(os.path.join(out, "synth.pack"), os.path.join(out, "synth_pack.h"), os.path.join(out, "synth.sounds"))
