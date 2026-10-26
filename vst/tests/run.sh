#!/usr/bin/env bash
# Offline x86 unit tests for MPC DJ FX (no device). Uses the host gcc when present, else the
# gcc:12 Docker image.
# The module tests are self-contained; the macro test links the whole engine and needs the framework
# wrapper (engine.h) and the generated build/params.h - it is skipped until vst/test.sh has run once.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"      # .../vst/tests
VST="$(cd "$HERE/.." && pwd)"              # .../vst
MV="${MPC_VST_PLUGINS:?set MPC_VST_PLUGINS to a checkout of sd88me/mpc-vst-plugins}"
CC="${CC:-gcc}"

MODS="iso repeat flanger limiter"

if command -v "$CC" >/dev/null 2>&1; then
  echo "== module tests (host $CC) =="
  for t in $MODS; do
    $CC -O2 -std=gnu11 -Wall -Wextra -I"$VST/include" -o "$HERE/${t}_test" "$HERE/${t}_test.c" "$VST/src/${t}.c" -lm
    "$HERE/${t}_test"; echo
  done
  if [ -f "$VST/build/params.h" ]; then
    echo "== macro test =="
    $CC -O2 -std=gnu11 -Wall -Wextra -I"$VST/include" -I"$MV/wrapper" -I"$VST/build" \
        -o "$HERE/macro_test" "$HERE/macro_test.c" "$VST"/src/*.c -lm
    "$HERE/macro_test"
  else
    echo "(macro_test skipped: run vst/test.sh once to generate build/params.h)"
  fi
else
  echo "== module tests + macro test (docker gcc:12) =="
  docker run --rm -v "$VST":/w -v "$MV":/mv -w /w gcc:12 sh -c '
    set -e
    for t in '"$MODS"'; do
      gcc -O2 -std=gnu11 -Wall -Wextra -Iinclude -o tests/${t}_test tests/${t}_test.c src/${t}.c -lm
      ./tests/${t}_test; echo
    done
    if [ -f build/params.h ]; then
      gcc -O2 -std=gnu11 -Wall -Wextra -Iinclude -I/mv/wrapper -Ibuild -o tests/macro_test tests/macro_test.c src/*.c -lm
      ./tests/macro_test
    else
      echo "(macro_test skipped: run vst/test.sh once to generate build/params.h)"
    fi
  '
fi
