#!/bin/sh
# HC1703L Plus - keep the clock right without the cloud. MIT License.
# The base hack sets the time once at boot (ntpd -q); p2pcam has no time client of its own outside the cloud.
# With the nightly restart off the clock would drift for weeks, so this re-syncs every 6 hours from the default
# gateway: with the cloud block in place it is the only time server the camera can reach. Nothing is written to
# flash. One copy per boot (lock); started by plus.sh from a RAM copy; the last result is in /tmp/clock.log.
trap '' HUP
mkdir /tmp/clock.lock 2>/dev/null || exit 0
BB=/tmp/hack/busybox
while :; do
  sleep 21600
  gw=$(route -n | awk '$1=="0.0.0.0" && $3=="0.0.0.0" && $4 ~ /G/ {print $2; exit}')
  [ -n "$gw" ] && [ -x $BB ] || continue
  $BB ntpd -q -n -p "$gw" > /tmp/clock.log 2>&1 &
  p=$!
  sleep 30
  kill $p 2>/dev/null
done
