#!/usr/bin/env bash
# MPC DJ FX - build the skin and render every page offline (no device needed).
#   vst/preview.sh  -> docs/ui-preview/page_0..N.png
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"          # .../vst
PROJECT="$(cd "$HERE/.." && pwd)"              # the port (contains docs/)
MV="${MPC_VST_PLUGINS:?set MPC_VST_PLUGINS to a checkout of sd88me/mpc-vst-plugins}"
FOLDER="LarryDaimond - VST - MPC DJ FX"

echo "== build skin =="
"$MV/tools/build_port.sh" "$HERE/vst.json" >/dev/null

mkdir -p "$PROJECT/docs/ui-preview"
echo "== render pages -> docs/ui-preview/page_*.png =="
docker run --rm -v "$MV":/mv:ro -v "$PROJECT":/w -w /w python:3.11-slim sh -c '
  pip install -q --no-warn-script-location Pillow >/dev/null 2>&1
  python3 /mv/tools/studio.py preview "/w/vst/build/skin/'"$FOLDER"'/Plugin Skins" -o "/w/docs/ui-preview/page_%d.png"
'
echo "done."
