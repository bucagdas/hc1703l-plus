#!/bin/sh
# HC1703L Plus motion-snap v4 - uses the camera's own motion/person recordings as the trigger. MIT License.
# When the camera detects motion it writes a "*M.mp4" clip to the SD card. This script watches for new
# clips, saves a snapshot for every new event and links it to the clip (motion/mo_*.clip) for the gallery.
# An optional <SD>/on_motion.sh hook is called as: on_motion.sh <snapshot> <clip>.
API="http://127.0.0.1:8001/snapshot"
BASE=/tmp/mnt/mmc01/0
OUT="$BASE/motion"
PIDF=/tmp/mo.pid
POLL=${POLL:-2}         # how often to scan for a new clip (seconds)
MAX=${MAX:-500}         # max event snapshots to keep
HOOK="$BASE/on_motion.sh"   # called on every event if present: on_motion.sh <snapshot> <clip>

if [ -f "$PIDF" ]; then
  op=$(cat "$PIDF" 2>/dev/null)
  if [ -n "$op" ] && kill -0 "$op" 2>/dev/null; then echo "already running (pid $op)"; exit 0; fi
fi
echo $$ > "$PIDF"
trap '' HUP
trap 'rm -f "$PIDF"; echo "stopped"; exit 0' TERM INT
mkdir -p "$OUT"

newest() { ls -1 $BASE/20[0-9][0-9]-*/*/*.mp4 2>/dev/null | tail -1; }
# :8090/ redirects to the CGI gallery.
gen_index() {
  echo '<html><head><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=/cgi-bin/gal"><title>HC1703L</title></head><body><a href="/cgi-bin/gal">Recordings</a></body></html>' > "$OUT/index.html"
}
cleanup() { n=0; for f in $(ls -1t "$OUT"/mo_*.jpg 2>/dev/null); do n=$((n+1)); [ $n -gt $MAX ] && rm -f "$f" "${f%.jpg}.clip"; done; }

gen_index
last=$(newest)
echo "motion-snap v4 started pid=$$ POLL=$POLL -> $OUT (last clip: ${last##*/})"
while true; do
  sleep "$POLL"
  cur=$(newest)
  if [ -n "$cur" ] && [ "$cur" != "$last" ]; then
    ts=$(date '+%Y%m%d_%H%M%S' 2>/dev/null || echo t$$)
    clip=${cur##*/}
    httpclt getfile "$API" "$OUT/mo_$ts.jpg" >/dev/null 2>&1
    echo "${cur#/tmp/mnt/mmc01/0/}" > "$OUT/mo_$ts.clip"   # relative path, so the gallery and mailers can find the clip
    echo "[$ts] MOTION EVENT: clip=$clip -> snapshot mo_$ts.jpg"
    cleanup; gen_index
    [ -x "$HOOK" ] && "$HOOK" "$OUT/mo_$ts.jpg" "$cur" >/dev/null 2>&1 &
    last="$cur"
  fi
done
