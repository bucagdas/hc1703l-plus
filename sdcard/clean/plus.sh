#!/bin/sh
# HC1703L Plus - start the user-facing services from the SD card. MIT License.
# Called by health.sh on every boot (stock or patched). Every step is idempotent.
R=/tmp/mnt/mmc01/0
C=$R/clean
BB=/tmp/hack/busybox
# 1) improved control panel on :8080 (bind-mounted over the base hack's webui, RAM only)
[ -f $C/www/kur.sh ] && sh $C/www/kur.sh
# 2) recordings gallery on :8090
if [ -x $BB ] && [ -f $R/motion/cgi-bin/gal ] && ! netstat -ltn 2>/dev/null | grep -q ':8090 '; then
  $BB httpd -p 8090 -h $R/motion
fi
# 3) motion snapshot service (mo.sh guards itself with a pid file)
if [ -f $R/mo.sh ] && ! ps | grep -q '[m]o\.sh'; then
  ( sh $R/mo.sh >> $R/mo.log 2>&1 & ) </dev/null
fi
# 4) the stock factory self-test loop (start.sh) checks for test files every second and, if it finds one, writes
#    flags to flash and cycles the lights and the speaker. Nothing on a normal camera needs it.
for p in $(ps | awk '/[a]uto_test\.sh/{print $1}'); do kill $p 2>/dev/null; done
# 5) clock re-sync every 6 hours (clock.sh takes its own lock)
if [ -f $C/clock.sh ] && [ ! -d /tmp/clock.lock ] && cp $C/clock.sh /tmp/clock.plus.sh; then
  ( sh /tmp/clock.plus.sh & ) </dev/null >/dev/null 2>&1
fi
