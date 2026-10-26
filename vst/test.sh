#!/usr/bin/env bash
# Offline x86 host test (ASan/UBSan) for this port; must print PASSED. No device needed.
# Uses the host gcc/g++ when present, else a Docker image.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST_PLUGINS:?set MPC_VST_PLUGINS to a checkout of sd88me/mpc-vst-plugins}"

"$MV/tools/test_port.sh" "$HERE/vst.json" "$@"
