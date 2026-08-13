# third_party/mi — Mutable Instruments

Vendored, unmodified, from Émilie Gillet's `eurorack` repository. MIT licensed;
see `LICENSE` and the header of every file, which carries its own notice.

    clouds/dsp/fx/reverb.h      the Clouds reverb  -> SPACE mode MI-CLOUD
    clouds/dsp/fx/fx_engine.h   the delay-line engine it is written against
    rings/dsp/fx/reverb.h       the Rings reverb   -> SPACE mode MI-RINGS
    rings/dsp/fx/fx_engine.h    the same engine, sized and formatted differently
    stmlib/                     the three headers those four need

`stmlib` came from a populated checkout (`DaisyExamples/stmlib`) because the
submodule inside `eurorack` is empty.

Nothing here is edited. Everything this project needs to bend them to its own
shapes lives in `src/dsp/mi_reverb.*`, which is the only file that includes
them. If these are ever updated from upstream, that adapter is the only thing
that should need looking at.

Both reverbs are the same Griesinger topology -- four allpass diffusers into a
loop of two by (two allpasses and a delay) -- with different lengths, different
storage formats, and different modulation. Clouds keeps its buffer as 12-bit
words and was written for 32 kHz; Rings keeps 16-bit and was written for 48.
The lengths are compile-time constants in samples, so running them at another
rate transposes the whole tail: at 48 kHz MI-RINGS is exactly the module, and
at 32 kHz so is MI-CLOUD. Anywhere else they are those reverbs played slow or
fast, which is a legitimate thing to want and is why the rate is selectable.
