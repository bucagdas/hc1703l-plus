#!/bin/sh
# HC1703L Plus - one-shot loader for the patched p2pcam. MIT License.
# Lives in the SD card root as /mnt/debug_cmd.sh. The stock start.sh runs it before sensor.sh and before
# p2pcam; /bak is writable at that moment. This script never writes to flash.
#
# Rules:
#  1. Nothing may abort the script half way. In ash ":" is a special builtin: a failed "`:` > file"
#     redirection kills the script, so the boot counters are written with touch.
#  2. If this file cannot delete itself there is no one-shot guarantee: boot STOCK. A sensor.sh line from
#     the base hack skips minihack while this file exists, so minihack is called here instead.
#  3. If the boot counter cannot be written persistently, boot STOCK: a failing boot must always count.
#  4. A RAM copy is bind-mounted only after its md5 matches the .md5 file (skipped if no .md5 file).
# health.sh puts this file back after a healthy boot. Three unhealthy boots in a row -> stays stock.
C=/mnt/clean
K=/tmp/debug_cmd.log
say(){ echo "$*" >> $K; }
md5ok(){
  [ -f "$2" ] || return 0
  read w < "$2"
  set -- $(md5sum "$1" 2>/dev/null)
  [ -n "$1" ] && [ "$1" = "$w" ]
}

rm -f /mnt/debug_cmd.sh
if [ -e /mnt/debug_cmd.sh ]; then
  say "cannot delete loader: stock + minihack"
  [ -f /home/minihack.sh ] && /home/minihack.sh
  exit 0
fi

# Cloud block runs on every boot, stock or patched. Copied to RAM: start.sh unmounts /mnt later.
if [ -f $C/netguard.sh ] && [ ! -f $C/NONETGUARD ]; then
  cp $C/netguard.sh /tmp/netguard.sh && ( sh /tmp/netguard.sh </dev/null >/dev/null 2>&1 & )
fi
# Health check + services, started once the card is mounted again by p2pcam (see boot.sh).
[ -f $C/boot.sh ] && cp $C/boot.sh /tmp/boot.plus.sh && ( sh /tmp/boot.plus.sh </dev/null >/dev/null 2>&1 & )

[ -f $C/DISABLE ] && exit 0
[ -f $C/a2 ] && touch $C/a3
[ -f $C/a1 ] && touch $C/a2
touch $C/a1
sync
if [ ! -f $C/a1 ]; then
  say "cannot write boot counter: stock"
  exit 0
fi

[ -f $C/p2pcam.sqfs ] || exit 0
S=/tmp/p2pcam.clean.sqfs
if cp $C/p2pcam.sqfs $S && md5ok $S $C/p2pcam.sqfs.md5 && mount --bind $S /bak/p2pcam.sqfs; then
  say "sqfs bound"
else
  rm -f $S
  say "sqfs not bound: stock"
  exit 0
fi

# The smaller video buffer pool is only safe at native 720p, so sensor.sh is bound only after the sqfs.
if [ -f $C/sensor.sh ]; then
  T=/tmp/sensor.clean.sh
  if cp $C/sensor.sh $T && md5ok $T $C/sensor.sh.md5 && sh -n $T && chmod 755 $T && mount --bind $T /bak/sensor.sh; then
    say "sensor.sh bound"
  else
    rm -f $T
    say "sensor.sh not bound: flash sensor.sh"
  fi
fi

# Motion settings (clean/md.conf, written by the panel). RAM copies only: the files on flash stay as they are,
# and a failure here just leaves the stock values in place.
if [ -f $C/md.conf ]; then
  if [ -f $C/bin/mdcfg ] && cp $C/bin/mdcfg /tmp/mdcfg && chmod 755 /tmp/mdcfg &&
     /tmp/mdcfg apply /home/devParam.dat /tmp/devParam.plus $C/md.conf &&
     mount --bind /tmp/devParam.plus /home/devParam.dat; then
    say "devParam bound"
  else
    say "devParam not bound: flash devParam"
  fi
  L=$(awk -F= '/^sensitivity=/{print $2}' $C/md.conf)
  case "$L" in
    0|2)
      if cp /bak/hwcfg.ini /tmp/hwcfg.plus && printf '\nmotion_level = %s\n' "$L" >> /tmp/hwcfg.plus &&
         mount --bind /tmp/hwcfg.plus /bak/hwcfg.ini; then
        say "hwcfg bound"
      else
        say "hwcfg not bound"
      fi ;;
  esac
fi
sync
