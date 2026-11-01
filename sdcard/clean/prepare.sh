#!/bin/sh
# HC1703L Plus - generate clean/sensor.sh and clean/sensor_day.ini from THIS camera's own stock files,
# so no vendor file has to be distributed. MIT License. Run once on the camera (telnet), any boot:
#   sh /tmp/mnt/mmc01/0/clean/prepare.sh [sharpness]      (sharpness 100-200, default 140)
# Every edit must match exactly once, otherwise nothing is written.
R=/tmp/mnt/mmc01/0
C=${PREP_OUT:-$R/clean}
SHP=${1:-140}
case "$SHP" in ''|*[!0-9]*) echo "sharpness must be a number"; exit 1 ;; esac
die(){ echo "prepare: $*"; umount /tmp/prep_bak 2>/dev/null; rm -f /tmp/prep.*; exit 1; }
cnt(){ grep -c -- "$1" "$2"; }

# On a patched boot /bak/sensor.sh is a RAM bind. A second bind of /bak shows the real flash file.
mkdir -p /tmp/prep_bak
mount --bind /bak /tmp/prep_bak || die "cannot bind /bak"
B=/tmp/prep_bak
[ -f $B/sensor.sh ] || die "no /bak/sensor.sh"

# --- sensor.sh -------------------------------------------------------------------------------------
cp $B/sensor.sh /tmp/prep.sh || die "copy sensor.sh"
# a) the base hack's line skips minihack while the loader exists; always run it instead
if grep -q '^\[ -f /home/minihack.sh \] && \[ ! -f /mnt/debug_cmd.sh \] && /home/minihack.sh' /tmp/prep.sh; then
  sed -i 's#^\[ -f /home/minihack.sh \] && \[ ! -f /mnt/debug_cmd.sh \] && /home/minihack.sh#[ -f /home/minihack.sh ] \&\& /home/minihack.sh#' /tmp/prep.sh
fi
[ "$(cnt '^\[ -f /home/minihack.sh \] && /home/minihack.sh$' /tmp/prep.sh)" = 1 ] || die "minihack line not found (install the base hack first)"
# b) 18 MB video buffer pool instead of 27 MB: enough for native 720p, frees ~11 MB of RAM
[ "$(cnt 'param_vb_size_mb=27' /tmp/prep.sh)" = 1 ] || die "vb.ko line not found"
sed -i 's/param_vb_size_mb=27/param_vb_size_mb=18/' /tmp/prep.sh
# c) after the stock ISP files are unpacked, take tuned copies from the SD card if they look complete
A='unlzma -c /bak/sensors/sensor_whilelight_${sensor_id}.ini.lzma > /tmp/sensor_whilelight.ini'
[ "$(grep -cF -- "$A" /tmp/prep.sh)" = 1 ] || die "ISP unpack line not found"
cat > /tmp/prep.ins <<'X'
# HC1703L Plus: tuned ISP files from the SD card, only if both first and last sections are present
for f in day whilelight; do
	if [ -f /mnt/clean/sensor_$f.ini ]; then
		cp /mnt/clean/sensor_$f.ini /tmp/sensor_$f.new && grep -q '^\[AE\]' /tmp/sensor_$f.new && grep -q '^\[TE\]' /tmp/sensor_$f.new && mv /tmp/sensor_$f.new /tmp/sensor_$f.ini
		rm -f /tmp/sensor_$f.new
	fi
done
X
awk -v a="$A" -v f=/tmp/prep.ins '{print} $0==a {while((getline l < f)>0) print l}' /tmp/prep.sh > /tmp/prep.sh2 || die "awk"
sh -n /tmp/prep.sh2 || die "generated sensor.sh has a syntax error"

# --- sensor_day.ini --------------------------------------------------------------------------------
I=$(ls $B/sensors/sensor_day_*.ini.lzma 2>/dev/null | head -n 1)
[ -n "$I" ] || die "no stock day ini"
unlzma -c $I > /tmp/prep.ini || die "unlzma"
[ "$(cnt '^frame_rate = 12.5' /tmp/prep.ini)" = 1 ] || die "frame_rate line"
[ "$(cnt '^slow_frame_rate = 12.5' /tmp/prep.ini)" = 1 ] || die "slow_frame_rate line"
[ "$(awk '/^\[SHP_V2\]/{f=1;next} /^\[/{f=0} f&&/^strength = /{n++} END{print n+0}' /tmp/prep.ini)" = 1 ] || die "SHP_V2 strength line"
# 25 FPS in day mode. slow_frame_rate must match: AE dropping to 12.5 in dim light makes recordings skip seconds.
sed -i 's/^frame_rate = 12.5/frame_rate = 25/; s/^slow_frame_rate = 12.5/slow_frame_rate = 25/' /tmp/prep.ini
sed -i "/^\[SHP_V2\]/,/^\[/ s/^strength = [0-9]*/strength = $SHP/" /tmp/prep.ini
grep -q '^\[AE\]' /tmp/prep.ini && grep -q '^\[TE\]' /tmp/prep.ini || die "generated ini incomplete"

umount /tmp/prep_bak
# --- write to the SD card: temp name, md5, rename ---------------------------------------------------
put(){ cp "$1" "$C/$2.new" && sync && [ "$(md5sum < "$1")" = "$(md5sum < "$C/$2.new")" ] && mv -f "$C/$2.new" "$C/$2" || die "write $2"; }
put /tmp/prep.sh2 sensor.sh
set -- $(md5sum $C/sensor.sh); echo "$1" > $C/sensor.sh.md5
put /tmp/prep.ini sensor_day.ini
sync
rm -f /tmp/prep.*
echo "prepare: wrote clean/sensor.sh, clean/sensor.sh.md5, clean/sensor_day.ini (sharpness $SHP)"
