#!/bin/bash
# 3DSilksong: convert your own copy of Silksong into the 3DS game data, start to finish, with one command
# (modelled on HK3DS's tools/build.sh):
#
#   tools/build.sh                  every step below, in order
#   tools/build.sh --only STEP      just one step (again, if it was done)
#   tools/build.sh --from STEP      forget STEP and every step after it, and run them again
#   tools/build.sh --rooms FILE     convert only the rooms listed in FILE (default: tools/data/all_rooms.txt)
#   tools/build.sh --list           the steps
#
# Environment: SILKSONG (the game folder), SSDATA (the exports, default ../ssdata), PYTHON (default python3),
# MKROMFS (default mkromfs3ds, else tools/mkromfs.py), LANGS (default "EN FR"), JOBS (rooms
# converted at once, default 6), FONTS_ALL=1 (also the Chinese, Japanese and Korean fonts).
# Each step logs to work/logs/<step>.log; finished steps are remembered in work/done/, so running the same command
# again carries on from the step that failed. The converted data goes to romfs/, then 3dsilksong.romfs.
set -o pipefail
TOP=$(cd "$(dirname "$0")/.." && pwd); T=$TOP/tools; W=$TOP/work; R=$TOP/romfs
STEPS="check inventory export index rooms prefabs fsm sprites items map shops invpane particles ui fonts music sfx text zones data compile"
ONLY=""; FROM=""; ROOMS=$T/data/all_rooms.txt
while [ $# -gt 0 ]; do
  case "$1" in
    --only) ONLY=$2; shift ;;
    --from) FROM=$2; shift ;;
    --rooms) ROOMS=$(cd "$(dirname "$2")" && pwd)/$(basename "$2"); shift ;;
    --list) echo "steps: $STEPS"; exit 0 ;;
    -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1 (tools/build.sh --help)"; exit 2 ;;
  esac
  shift
done
PY=${PYTHON:-python3}
export SSDATA=${SSDATA:-$TOP/../ssdata}
mkdir -p "$W/logs" "$W/done" "$R"

fail() { echo; echo "FAILED: $1  (log: $2)"; tail -n 15 "$2" | sed 's/^/    /'; echo "Fix that and run the same command again."; exit 1; }

step_check() {
  [ -n "$SILKSONG" ] || { echo "set SILKSONG to the game folder"; return 1; }
  $PY -c "import numpy, PIL, yaml, cryptography" || { echo "python -m pip install -r requirements.txt"; return 1; }
  $PY -c "import UnityPy" || echo "warning: no UnityPy (only tools/bundle_deps.py needs it)"
  command -v ffmpeg >/dev/null || { echo "no ffmpeg on PATH (the music step needs it)"; return 1; }
}
step_inventory() { (cd "$T" && $PY inventory.py "$SILKSONG"); }
step_export()    { "$T/export_game.sh" "$SILKSONG"; }
step_index()     { (cd "$T" && $PY index_export.py); }
step_rooms() {
  # JOBS rooms at once (default 6; each takes 1-3 GB). Rooms already converted are skipped, so a rerun carries on.
  # A room that fails is listed in work/rooms_failed.txt and doesn't stop the build.
  local todo=$W/rooms_todo.txt
  grep -v '^\s*#' "$ROOMS" | grep . | while read -r r; do [ -f "$R/rooms/$r.ssr" ] || echo "$r"; done > "$todo"
  echo "$(wc -l < "$todo") rooms to convert, ${JOBS:-6} at once"
  (cd "$T" && $PY -c "import sscommon; sscommon.guidmap()")      # the shared guid index, once, before the jobs
  (cd "$T" && xargs -P "${JOBS:-6}" -I{} sh -c "$PY build_room.py '$R' '{}' || echo '{}' >> '$W/rooms_failed.txt'" < "$todo")
  local ok; ok=$(grep -v '^\s*#' "$ROOMS" | grep . | while read -r r; do [ -f "$R/rooms/$r.ssr" ] && echo; done | wc -l)
  echo "rooms: $ok of $(grep -v '^\s*#' "$ROOMS" | grep -c .) converted" \
    "$([ -s "$W/rooms_failed.txt" ] && echo "(failed: $(sort -u "$W/rooms_failed.txt" | tr '\n' ' '))")"
}
step_prefabs()   { (cd "$T" && $PY build_prefabs.py "$R" --jobs "${JOBS:-6}"); }   # what the scripts spawn (rooms/pf_*)
step_fsm()       { (cd "$T" && $PY build_fsm.py "$R"); }   # the rooms' FSMs for the engine (rooms/*.ssf)
step_sprites() {
  # tools/data/sprites.txt: one set per line, "<file>|<library>[+<library>...]|<clips, or empty for all>"
  grep -v '^\s*#' "$T/data/sprites.txt" | grep . | while IFS='|' read -r out lib clips; do
    (cd "$T" && $PY build_sprites.py "$R/sprites/$out.sss" "$lib" ${clips:+--clips "$clips"}) || exit 1
  done || return 1
  # and every animation library the rooms' objects use (enemies, NPCs, interactables...)
  (cd "$T" && $PY build_sprites.py --objects "$R" --jobs "${JOBS:-6}")
}
step_map()       { (cd "$T" && $PY build_map.py "$R"); }      # the game's map (ui/map_<zone>.sss, text/map.txt)
step_invpane()   { (cd "$T" && $PY build_inventory.py "$R"); }   # the inventory pane (text/inventory.txt, ui/inv_pane.sss)
step_shops()     { (cd "$T" && $PY build_shops.py "$R"); }    # the merchants' stock (text/shops.txt, ui/shop_items.sss)
step_items()     { (cd "$T" && $PY build_items.py "$R"); }   # rosaries, HUD spool pieces, their sounds
step_particles() { (cd "$T" && $PY build_particles.py "$R"); }
step_ui()        { (cd "$T" && $PY build_ui.py "$R"); }
step_fonts()     { (cd "$T" && $PY build_font.py "$R" ${FONTS_ALL:+--all}); }
step_music() {
  # the cues as Ogg, then as PCM for the engine (no decoder on the 3DS), and which room plays which
  (cd "$T" && $PY build_music.py "$R" --fsm-sfx && $PY build_music_pcm.py "$R" && $PY music_map.py "$R")
}
step_sfx()       { (cd "$T" && $PY build_sfx_missing.py "$R"); }   # the scripts' and the engine's other sounds
step_text()      { (cd "$T" && $PY build_text.py "$R" ${LANGS:-EN FR}); }
step_zones()     { (cd "$T" && $PY build_zones.py "$R"); }    # each room's area name (text/zones.txt)
step_data() {      # the same as "make data", without needing make (it comes with devkitPro)
  sed -n 's/.*SS_VERSION "\(.*\)"/\1/p' "$TOP/source/version.h" > "$R/version.txt"
  local mk=${MKROMFS:-$(command -v mkromfs3ds || echo "$PY $T/mkromfs.py")}
  (cd "$TOP" && $mk romfs 3dsilksong.romfs) && ls -l "$TOP/3dsilksong.romfs"
}
step_compile() {
  if [ -z "$DEVKITARM" ]; then echo "no devkitPro (DEVKITARM unset): skipped, the game data is ready"; return 0; fi
  (cd "$TOP" && make && make cia)
}

if [ -n "$FROM" ]; then
  drop=0; for s in $STEPS; do [ "$s" = "$FROM" ] && drop=1; [ $drop = 1 ] && rm -f "$W/done/$s"; done
fi
for s in $STEPS; do
  [ -n "$ONLY" ] && [ "$s" != "$ONLY" ] && continue
  [ -z "$ONLY" ] && [ -f "$W/done/$s" ] && { echo "= $s (done)"; continue; }
  echo "> $s"
  LOG=$W/logs/$s.log
  "step_$s" > "$LOG" 2>&1 || fail "$s" "$LOG"
  tail -n 1 "$LOG" | sed 's/^/  /'
  touch "$W/done/$s"
done
echo "all done: $TOP/3dsilksong.romfs"
