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

## Installing on Move

Crest FX needs a Move with [Schwung](https://github.com/charlesvestal/schwung) installed.

1. Download `crest-fx-module.tar.gz` from the
   [latest release](https://github.com/klaine/crest-fx/releases/latest).
2. Install it with Schwung Manager (`http://move.local:7700`), or by hand: extract the archive so that
   it lands at `/data/UserData/schwung/modules/audio_fx/crest-fx/` on the Move.
3. Rescan modules in Schwung Manager, or restart Schwung.

Crest FX then appears in the audio FX list as **CRFX**.

## Building

```sh
scripts/build.sh           # needs zig and python3; tests, then writes crest-fx-module.tar.gz
scripts/deploy.sh [host]   # staged install of that build over SSH, default move.local
```

`scripts/deploy.sh` needs an SSH key on the Move (add one at `http://move.local/development/ssh`). It
stages the package on the Move's data partition and renames it into place, so a running Schwung is not
disturbed. `scripts/logs.sh` follows Schwung's debug log.

## Licence

MIT. The effects come from Crest and were adapted from Amino, both by the same author. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for Schwung's API headers.
