#!/bin/sh
# HC1703L Plus - health check for the patched p2pcam. MIT License.
# Started in the background on every boot (base hack's minihack.sh, or plus.sh setups).
# Two stages:
#   T+ARM     if the patched build is healthy, put the loader back (unless a3 exists). Counters are KEPT.
#   T+STABLE  if it is still healthy, clear the counters; the loader stays armed.
# A build that crashes at minute 10 still counts: after 3 failed boots the camera stays stock.
# The loader is installed atomically: temp name, sync, md5, rename, sync. A half file never lands in /.
# Disable: clean/DISABLE. Reset: delete clean/a1 a2 a3.
trap '' HUP
# One instance per boot: it may be started by boot.sh and by an optional minihack hook.
mkdir /tmp/health.lock 2>/dev/null || exit 0
R=/tmp/mnt/mmc01/0
C=$R/clean
L=$C/boot.log
ARM=${ARM:-300}
STABLE=${STABLE:-1800}

# User-facing services (panel, gallery, motion snapshots). Background; a failure cannot affect this script.
[ -f $C/plus.sh ] && ( sh $C/plus.sh ) </dev/null >/dev/null 2>&1 &

log(){
  read u _ < /proc/uptime
  echo "$(date '+%m-%d %H:%M:%S') up=${u%%.*} $*" >> $L
}

arm(){
  n=$R/debug_cmd.new
  rm -f $n
  if ! cp $C/debug_cmd.sh $n; then
    rm -f $n
    log "arm: cp failed"
    return 1
  fi
  sync
  set -- $(md5sum $C/debug_cmd.sh 2>/dev/null)
  a=$1
  set -- $(md5sum $n 2>/dev/null)
  if [ -z "$a" ] || [ "$a" != "$1" ]; then
    rm -f $n
    log "arm: md5 mismatch"
    return 1
  fi
  if ! mv -f $n $R/debug_cmd.sh; then
    rm -f $n
    log "arm: mv failed"
    return 1
  fi
  sync
  return 0
}

# Remove a broken or outdated loader. We only get here when minihack was able to run.
temizle(){
  [ -e $R/debug_cmd.sh ] || return 0
  set -- $(md5sum $C/debug_cmd.sh 2>/dev/null)
  a=$1
  set -- $(md5sum $R/debug_cmd.sh 2>/dev/null)
  if [ -n "$a" ] && [ "$a" != "$1" ]; then
    rm -f $R/debug_cmd.sh
    log "loader in SD root differs from clean/, removed"
  fi
}

denetle(){
  set -- $(md5sum /p2pcam/p2pcam 2>/dev/null)
  got=$1
  ok=1
  ps | grep -q '[/]p2pcam/p2pcam' || ok=0
  netstat -ant 2>/dev/null | grep -q ':554 ' || ok=0
}

tail -n 100 $L > $L.t 2>/dev/null && [ -s $L.t ] && mv $L.t $L
if [ -f $C/DISABLE ]; then log "DISABLE present, not arming"; exit 0; fi
temizle

sleep $ARM

want=
read want < $C/p2pcam.md5
if [ -z "$want" ]; then
  log "cannot read p2pcam.md5, not arming"
  exit 0
fi

route -n 2>/dev/null | grep -q '!' || log "WARNING: no reject route (cloud block missing)"

denetle
# A RAM copy means this boot is patched. A stale p2pcam.md5 must not demote a healthy build.
if [ -f /tmp/p2pcam.clean.sqfs ] && [ "$got" != "$want" ]; then
  log "WARNING: p2pcam.md5 does not match the running binary, treating as patched"
  want=$got
fi
if [ "$got" = "$want" ]; then
  if [ $ok != 1 ]; then
    log "patched boot UNHEALTHY, loader not armed"
    exit 0
  fi
  if [ -f $C/a3 ]; then
    log "patched healthy, a3 present: waiting for stability, not armed"
  else
    arm && log "patched healthy, loader armed"
  fi
  sleep $((STABLE-ARM))
  denetle
  if [ "$got" = "$want" ] && [ $ok = 1 ]; then
    rm -f $C/a1 $C/a2 $C/a3
    arm && log "stable: counters cleared"
  else
    log "not healthy at T+STABLE, counters kept"
  fi
else
  if [ -f $C/a3 ]; then
    log "STOCK: 3 attempts failed, giving up (delete clean/a? to retry)"
  else
    arm && log "STOCK boot, loader armed for the next boot"
  fi
fi
