#!/bin/sh
# HC1703L Plus - copy the improved panel from the SD card to RAM and bind-mount it over the base hack's
# webui. Never writes to flash. Called on every boot by plus.sh. Undo: umount /tmp/hack/www/cgi-bin/webui
# If any check fails nothing is changed and the original page keeps working. MIT License.
# Disable: touch <SD>/clean/www/DISABLE
W=/tmp/hack/www/cgi-bin/webui
S=/tmp/mnt/mmc01/0/clean/www/webui
T=/tmp/webui.sd
[ -f $W ] && [ -f $S ] || exit 0
[ -f /tmp/mnt/mmc01/0/clean/www/DISABLE ] && exit 0
cp $S $T.new || { rm -f $T.new; exit 1; }
sh -n $T.new && grep -q 'HC1703L-WEBUI-SD' $T.new || { rm -f $T.new; exit 1; }
chmod 755 $T.new
# The panel may be running right now, which keeps the mount busy: detach it lazily. "$W" without the trailing
# space also matches a stale entry, listed as "$W\\040(deleted)". Without this, mv and mount --bind both fail.
if grep -q " $W" /proc/mounts; then
  umount $W 2>/dev/null || umount -l $W || { rm -f $T.new; exit 1; }
fi
mv -f $T.new $T && mount --bind $T $W
