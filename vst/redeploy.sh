#!/usr/bin/env bash
# MPC DJ FX - build + deploy to the MPC.
#   vst/redeploy.sh                build -> deploy -> restart acvs
#   vst/redeploy.sh --no-restart   build + deploy only (needs a re-insert, no MPC restart)
#   DEVICE=<mpc-ip> vst/redeploy.sh
#
# First install/registration goes through tools/release.py + the packaged install.sh (stops MPC, backs
# up MPC.settings, adds the plugin-list entry). This script is for UPDATES to an already-registered
# plugin: same file= path, so no settings edit is needed.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"     # .../vst
MV="${MPC_VST_PLUGINS:?set MPC_VST_PLUGINS to a checkout of sd88me/mpc-vst-plugins}"
DEVICE="${DEVICE:?set DEVICE to the MPC IP address}"
FOLDER="LarryDaimond - VST - MPC DJ FX"
SO="mpc_djfx.so"

echo "== 1/2 build (skin + .so) =="
"$MV/tools/build_port.sh" "$HERE/vst.json"

SKIN="$HERE/build/skin/$FOLDER"
[ -d "$SKIN" ] || { echo "skin folder not found: $SKIN" >&2; exit 1; }
cp "$HERE/build/$SO" "$SKIN/$SO"

echo "== 2/2 deploy to $DEVICE =="
tar -C "$HERE/build/skin" -cf - "$FOLDER" | ssh -o StrictHostKeyChecking=no "root@$DEVICE" \
  "tar -C /sdcard/Synths -xf -"
ssh -o StrictHostKeyChecking=no "root@$DEVICE" \
  "echo -n 'device md5: '; md5sum '/sdcard/Synths/$FOLDER/$SO' | cut -d' ' -f1"
echo -n "local  md5: "; md5sum "$SKIN/$SO" | cut -d' ' -f1

echo "== presets -> device Insert Effects =="
PRESET_STAGE="$(mktemp -d)"
python3 "$HERE/gen_presets.py" --out-dir "$PRESET_STAGE" --so-path "/sdcard/Synths/$FOLDER/$SO" >/dev/null
ssh -o StrictHostKeyChecking=no "root@$DEVICE" "mkdir -p '/sdcard/MPC Documents/Plugin Presets/Insert Effects/$FOLDER'"
tar -C "$PRESET_STAGE" -cf - . | ssh -o StrictHostKeyChecking=no "root@$DEVICE" \
  "tar -C '/sdcard/MPC Documents/Plugin Presets/Insert Effects/$FOLDER' -xf -"
rm -rf "$PRESET_STAGE"
echo -n "device presets: "; ssh -o StrictHostKeyChecking=no "root@$DEVICE" \
  "ls '/sdcard/MPC Documents/Plugin Presets/Insert Effects/$FOLDER/'/*.xpl | wc -l"

if [ "${1:-}" = "--no-restart" ]; then
  echo "== restart skipped (--no-restart); re-insert the plugin to load a new .so/skin =="
  exit 0
fi
echo "== restart acvs (MPC) =="
ssh -o StrictHostKeyChecking=no "root@$DEVICE" "systemctl restart acvs"
sleep 12
ssh -o StrictHostKeyChecking=no "root@$DEVICE" "echo -n 'acvs: '; systemctl is-active acvs"
echo "done."
