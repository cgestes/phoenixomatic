# third_party/daisysp — DaisySP

Vendored, unmodified, from Electrosmith's DaisySP. MIT licensed; see `LICENSE`
and the header of every file.

    Noise/clockednoise.*    -> CHAOS mode CLOCKED
    Effects/pitchshifter.h  -> SHIMMER's alternative shifter
    Control/phasor.*        the pitchshifter's crossfade ramps
    Utility/dsp.h           fclamp, the BLEP helpers, DSY_COUNTOF
    Utility/delayline.h     the pitchshifter's two delay lines

Two of these are themselves ports of Émilie Gillet's code -- clockednoise comes
from Plaits and says so in its own header -- which is why the copyright lines
name both.

Nothing here is edited. `src/dsp/daisy_bits.*` is the only file that includes
them, so an update from upstream is a copy and a look at one adapter.

A note on size: `pitchshifter.h` reserves two delay lines of 16384 floats, so
it costs 128 KB wherever it is compiled in. That is affordable on desktop and
web and is not on the Cardputer, which is why the adapter falls back to the
machine's own shifter when PHX_EMBEDDED is set rather than pretending the
choice exists there.
