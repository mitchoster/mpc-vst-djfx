#!/usr/bin/env bash
# Build this port with the upstream mpc-vst-plugins toolchain.
#   vst/build.sh [extra args]
# Set MPC_VST_PLUGINS to the framework checkout if it is not the default sibling.
#
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST_PLUGINS:?set MPC_VST_PLUGINS to a checkout of sd88me/mpc-vst-plugins}"

"$MV/tools/build_port.sh" "$HERE/vst.json" "$@"

# named presets beside the skin build (see gen_presets.py)
MPC_VST_PLUGINS="$MV" python3 "$HERE/gen_presets.py" >/dev/null
