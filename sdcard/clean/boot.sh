#!/bin/sh
# HC1703L Plus - started from RAM by debug_cmd.sh on every loader boot. MIT License.
# Waits until p2pcam has mounted the SD card, then runs health.sh from a RAM copy (so the card can be
# unmounted or rewritten while it runs). health.sh takes a lock, a second starter is harmless.
trap '' HUP
R=/tmp/mnt/mmc01/0
n=0
until grep -q " $R " /proc/mounts || [ $n -ge 90 ]; do sleep 2; n=$((n+1)); done
grep -q " $R " /proc/mounts || exit 0
[ -f $R/clean/health.sh ] && cp $R/clean/health.sh /tmp/health.plus.sh && exec sh /tmp/health.plus.sh
