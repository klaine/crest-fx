#!/bin/sh
# Build Crest FX with zig cc: codegen check, contract, card and native tests, the aarch64 crest-fx.so
# and crest-fx-module.tar.gz. Run from anywhere:
#
#   scripts/build.sh            # checks + tests + device build + package
#   scripts/build.sh --gen      # run tools/gen/gen.py first
#   scripts/build.sh --no-test  # skip the tests
#   scripts/build.sh --profile  # crest-fx.so with the cpu_us debug key (not for release)
#
# Needs zig (brew install zig) and python3. Set ZIG=/path/to/zig or PYTHON=... to override the lookup.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

GEN=0
TEST=1
PROFILE=0
for arg in "$@"; do
  case "$arg" in
    --gen) GEN=1 ;;
    --no-test) TEST=0 ;;
    --profile) PROFILE=1 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

PY=${PYTHON:-python3}
BUILD="$ROOT/build"
MODULE="$ROOT/modules/audio_fx/crest-fx"
mkdir -p "$BUILD" "$MODULE"

find_zig() {
  if [ -n "${ZIG:-}" ]; then echo "$ZIG"; return; fi
  if command -v zig >/dev/null 2>&1; then command -v zig; return; fi
  "$PY" -c 'import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), "zig"))' 2>/dev/null || true
}
ZIG=$(find_zig)
if [ -z "$ZIG" ] || [ ! -x "$ZIG" ]; then
  echo "zig not found: brew install zig (or set ZIG=...)" >&2
  exit 1
fi
echo "== zig $("$ZIG" version)"

# Same math flags on the device and in the tests: -ffast-math without assuming finite values, so the
# NaN guard in process_block keeps working.
WARN="-Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wno-unused-parameter"
MATH="-ffast-math -fno-finite-math-only"
DEFS=""
[ "$PROFILE" = 1 ] && DEFS="-DCREST_FX_PROFILE"
DSP_SRC="src/dsp/crest_fx.c src/dsp/fx.c src/dsp/params_gen.c"

if [ "$GEN" = 1 ]; then
  echo "== gen"
  "$PY" tools/gen/gen.py
else
  "$PY" tools/gen/gen.py --check
  "$PY" tools/gen/gen.py >/dev/null  # build/ is not committed: the contract's JSON
fi

# fx.c is a copy of Crest's. Differing can be right (a fix here not yet there), so this only says so.
SIBLING="$ROOT/../Crest for Move/src/dsp"
for f in fx.c fx.h; do
  if [ -f "$SIBLING/$f" ] && ! cmp -s "$SIBLING/$f" "src/dsp/$f"; then
    echo "== note: src/dsp/$f differs from Crest's"
  fi
done

if [ "$TEST" = 1 ]; then
  echo "== contract"
  "$PY" tests/contract/validate_contract.py
  if command -v node >/dev/null 2>&1; then
    node tests/device/test_cards.mjs
  else
    echo "== card tests: skipped (node missing)"
  fi

  echo "== native tests"
  SAN="-fsanitize=undefined -fno-sanitize-recover=undefined"
  "$ZIG" cc -std=c11 -O1 -g $SAN $WARN $MATH -I src/dsp \
    -o "$BUILD/crest_fx_test" $DSP_SRC tests/host/test_main.c -lm
  "$BUILD/crest_fx_test"
fi

echo "== crest-fx.so (aarch64-linux-gnu)"
# -ffile-prefix-map: no absolute build paths (and so no user name) in shipped binaries.
"$ZIG" cc -target aarch64-linux-gnu.2.28 -mcpu=cortex_a72 -std=c11 -O2 $WARN $MATH $DEFS \
  -fPIC -shared -fvisibility=hidden -s -ffile-prefix-map="$ROOT"=. -I src/dsp \
  -o "$MODULE/crest-fx.so" $DSP_SRC -lm
rm -f "$MODULE/crest-fx.so.o"

echo "== check crest-fx.so"
"$PY" scripts/check_so.py "$MODULE/crest-fx.so"

cp src/module.json src/cards.js LICENSE "$MODULE/"

echo "== package"
# COPYFILE_DISABLE keeps macOS tar from adding ._ AppleDouble files
COPYFILE_DISABLE=1 tar -czf "$ROOT/crest-fx-module.tar.gz" --exclude '.DS_Store' \
  -C "$ROOT/modules/audio_fx" crest-fx

ls -l "$MODULE/crest-fx.so" "$ROOT/crest-fx-module.tar.gz"
echo "built $ROOT/crest-fx-module.tar.gz"
