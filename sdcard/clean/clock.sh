#!/bin/sh
# HC1703L Plus - keep the clock right without the cloud. MIT License.
# The camera has no battery clock and the base hack does not set the time, so after a boot the clock starts at 1970.
# The stock p2pcam only gets the time from the cloud, which the cloud block shuts off.
# This script asks the default gateway (the router) over NTP: at boot until it answers (every 30 s for 10 minutes),
# then every 6 hours. With the cloud block in place the gateway is the only time server the camera can reach.
# A router without NTP: the panel sets the clock from the browser instead (Maintenance card, "Automatic").
# clean/clock.conf "clock=manual": the user sets the time in the panel; this script then leaves the clock alone.
# Nothing is written to flash. One copy per boot (lock); started by plus.sh from a RAM copy.
# /tmp/clock.log holds the last ntpd output, /tmp/clock.src where the time came from (read by the panel).
trap '' HUP
mkdir /tmp/clock.lock 2>/dev/null || exit 0
BB=/tmp/hack/busybox
CONF=/tmp/mnt/mmc01/0/clean/clock.conf

manual(){ grep -q '^clock=manual' $CONF 2>/dev/null; }
sync_once(){
  manual && return 0
  gw=$(route -n | awk '$1=="0.0.0.0" && $3=="0.0.0.0" && $4 ~ /G/ {print $2; exit}')
  [ -n "$gw" ] && [ -x $BB ] || return 1
  $BB ntpd -q -n -p "$gw" > /tmp/clock.log 2>&1 &
  p=$!
  ( sleep 30; kill $p 2>/dev/null ) </dev/null >/dev/null 2>&1 &
  k=$!
  wait $p
  r=$?
  kill $k 2>/dev/null
  [ $r = 0 ] || return 1
  echo "ntp $gw" > /tmp/clock.src
}

n=0
until sync_once || [ $n -ge 20 ]; do
  n=$((n+1))
  sleep 30
done
while :; do
  sleep 21600
  sync_once
done
