# Crest FX: agent notes

Crest's effects chain as a Schwung `audio_fx` (API v2), module id `crest-fx`. Read
[README.md](README.md) for the product. This file covers working on the code.

## Commands

```sh
scripts/build.sh           # gen --check, contract, cards and native tests, crest-fx.so, check_so.py, package
scripts/build.sh --gen     # regenerate first (after editing tools/gen/gen.py)
scripts/build.sh --profile # crest-fx.so with a read-only cpu_us key (render time in µs)
scripts/deploy.sh [host]   # staged install on a Move; scripts/logs.sh follows the debug log
```

## Generated files (don't hand-edit)

`tools/gen/gen.py` holds the parameters, pages and presets, and writes `src/dsp/params_gen.*`, the
GENERATED META block in `src/cards.js` and `build/*.json`. The build runs `gen.py --check` and **fails if
anything is stale**.

## Rules

- **`src/dsp/fx.c`, `fx.h` and `dsp_util.h` are copies of Crest's**, kept byte-identical where possible so
  a `diff` shows any drift; the build prints a note if `fx.c` or `fx.h` differ. A fix here may apply to
  Crest, and through it to Amino's `src/dsp/fx.c`: check them and tell the user. Parameter keys, ranges and
  options match Crest's because `fx.c` reads them through the same `P_*` names.
- **Audio FX differ from the workspace's other modules:** the chain host opens
  `modules/audio_fx/<id>/<id>.so` and ignores `dsp`, so the library is `crest-fx.so`, not `dsp.so`, and
  the entry point is `move_audio_fx_init_v2` (`src/dsp/audio_fx_api.h`, written from Schwung's docs).
  `tests/contract/validate_contract.py` checks the name, because a wrong one fails with nothing on screen.
- **Init must stay bit-exact:** `all_off` in `src/dsp/crest_fx.c` skips the block, and the tests compare it
  sample for sample. Preset 0 is Init.
- **Flags:** `-Wall -Wextra -Werror -Wshadow -Wdouble-promotion`, and `-ffast-math
  -fno-finite-math-only` on device and in tests alike (the NaN guard in `process_block` depends on it).
  Native tests run under UBSan.
- **Realtime:** no I/O, allocation or logging. Instances come from a static pool of 12 (1.1 MB each,
  resident only once used). `scripts/check_so.py` whitelists the library's undefined symbols.
- Nothing has run on a Move yet: don't claim device behaviour is verified.
