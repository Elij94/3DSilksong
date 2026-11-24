#!/bin/bash
# Export your own copy of Silksong with AssetRipper (2.0.0, headless), into $SSDATA:
#   ex/               "Export Unity Project": the scenes, prefabs and assets as Unity YAML (ExportedProject/Assets)
#   pc/               "Export Primary Content" (only with PRIMARY=1): textures, sprites, audio as plain files
#   Assembly-CSharp/  the game's decompiled scripts (read to understand components; never copied into the port)
#
#   tools/export_game.sh "C:/XboxGames/Hollow Knight- Silksong"
#
# Environment: ASSETRIPPER (the .exe, default C:/HK3DS/tools/AssetRipper/AssetRipper.GUI.Free.exe), SSDATA (default
# ../ssdata next to the repository), PORT (default 5556, so it can run next to an HK3DS export on 5555).
# Needs a lot of memory (the game is 7.7 GB of bundles: plan 16-24 GB free) and ~60 GB of disk.
set -eo pipefail
TOP=$(cd "$(dirname "$0")/.." && pwd)
GAME=${1:-$SILKSONG}
[ -n "$GAME" ] || { echo "usage: $0 <Silksong install folder>"; exit 2; }
DATA=""
for d in "$GAME"/*_Data "$GAME"/Content/*_Data; do [ -f "$d/globalgamemanagers" ] && { DATA=$d; break; }; done
[ -f "$DATA/globalgamemanagers" ] || { echo "no Silksong _Data folder in $GAME"; exit 1; }
AR=${ASSETRIPPER:-C:/HK3DS/tools/AssetRipper/AssetRipper.GUI.Free.exe}
PORT=${PORT:-5556}
SSDATA=${SSDATA:-$TOP/../ssdata}
mkdir -p "$SSDATA"; SSDATA=$(cd "$SSDATA" && pwd -W 2>/dev/null || pwd)
DATAW=$(cd "$DATA" && pwd -W 2>/dev/null || pwd)
LOG=$SSDATA/assetripper.log
URL=http://127.0.0.1:$PORT

echo "game data: $DATAW"
echo "exports:   $SSDATA   (log: $LOG)"
"$AR" --headless --port "$PORT" > "$LOG" 2>&1 &
ARPID=$!
trap 'kill $ARPID 2>/dev/null' EXIT
for i in $(seq 60); do curl -s -o /dev/null "$URL/" && break; sleep 1; done

post() {   # post <endpoint> <path>: AssetRipper answers when the job is done
  echo "$(date +%T) $1 $2"
  # AssetRipper takes a URL-encoded form (multipart gets 415), and answers with a redirect once the job is done
  curl -s -f -X POST --data-urlencode "path=$2" "$URL$1" -o /dev/null --max-time 86400
}
# Settings. The Addressables bundles carry no engine version (stripped to 0.0.0): without DefaultVersion AssetRipper
# can't read a single object in them (everything comes out "Unreadable", and the project export skips the scenes).
# The form replaces every setting: unchecked boxes are left out (= false).
echo "$(date +%T) settings (DefaultVersion ${SS_UNITY:-6000.0.50f1})"
curl -s -f -X POST "$URL/Settings/Update" -o /dev/null \
  --data-urlencode "DefaultVersion=${SS_UNITY:-6000.0.50f1}" --data-urlencode "TargetVersion=0.0.0a0" \
  -d PreferOriginalTextureExtension=true -d BundledAssetsExportMode=DirectExport -d ScriptContentLevel=Level2 \
  -d AudioExportFormat=Default -d ImageExportFormat=Png -d LightmapTextureExportFormat=Yaml -d SpriteExportMode=Yaml \
  -d ShaderExportMode=Dummy -d TextExportMode=Parse -d ScriptLanguageVersion=AutoSafe -d ScriptExportMode=Hybrid
post /LoadFolder "$DATAW"
if [ ! -d "$SSDATA/ex" ]; then
  post /Export/UnityProject "$SSDATA/project"
  mv "$SSDATA/project/ExportedProject/Assets" "$SSDATA/ex"
  [ -d "$SSDATA/ex/Scripts/Assembly-CSharp" ] && mv "$SSDATA/ex/Scripts/Assembly-CSharp" "$SSDATA/Assembly-CSharp"
fi
# The converters read only ex/ (and Assembly-CSharp/): the primary content export (4.3 million files for Silksong,
# tens of GB) is optional, PRIMARY=1 to make it.
if [ "${PRIMARY:-0}" = 1 ] && [ ! -d "$SSDATA/pc" ]; then
  post /Export/PrimaryContent "$SSDATA/primary"
  mv "$SSDATA/primary/Assets" "$SSDATA/pc"
fi
echo "$(date +%T) done: $SSDATA/ex"
