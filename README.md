# Crest FX

[Crest](https://github.com/klaine/crest)'s effects as a separate audio FX module for
[Schwung](https://github.com/charlesvestal/schwung) on Ableton Move. Use it on an effects slot, for
example to process an external instrument coming in through a Line In module on the synth slot:

```
Line In (synth slot) -> Crest FX (audio FX slot) -> track output
```

It also works behind any other sound generator, and as a Master FX.

- **Distortion:** soft, hard, fold and crush, anti-aliased.
- **Chorus:** two chorus types and an ensemble.
- **Delay:** free or tempo-synced (follows the Move's tempo), stereo, ping-pong or mono, with an L/R ratio.
- **Reverb:** room, hall and plate.
- **Routing:** parallel or any serial order of chorus, delay and reverb, with dry and wet levels.
- **8 presets,** one role each: Init, Room, Hall, Plate, Slapback, Dotted Echo, Ping Pong and Wash.
  Init has everything off: the signal passes through bit for bit until an effect is turned up.

The first knob page is the one you play. Knobs 1-4 are the mixer, one level per effect; knobs 5-8 are
delay feedback and sync, reverb decay, and a Wet knob that pulls chorus, delay and reverb out at
once (distortion sits in front of that blend, so it stays). The other pages hold every parameter.

The dry signal is never coloured: with Wet at 0 the output is the input, bit for bit, and anything
the effects add is only clamped at full scale. Tails keep running through silence, and an effect that
has been at level 0 for 2 s is switched off to save CPU, which cuts anything left of its tail.

**Status:** runs on a Move behind a Line In module; the native tests cover the rest.

## Install

```sh
scripts/build.sh           # needs zig and python3; writes crest-fx-module.tar.gz
scripts/deploy.sh [host]   # default move.local
```

Then rescan modules in Schwung Manager (`http://move.local:7700`) or restart Schwung. Crest FX appears
in the audio FX list. It installs to `/data/UserData/schwung/modules/audio_fx/crest-fx/`.

## Licence

MIT. The effects come from Crest and were adapted from Amino, both by the same author. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for Schwung's API headers.
