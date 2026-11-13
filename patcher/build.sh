#!/usr/bin/env bash
# HC1703L Plus - build the patched p2pcam package. MIT License, see ../LICENSE.
#
# Input : the stock /bak/p2pcam.sqfs copied from YOUR camera (we do not distribute vendor binaries).
# Output: <outdir>/p2pcam.sqfs, p2pcam.sqfs.md5, p2pcam.md5 -> copy them to the SD card's clean/ folder.
#
# Two stock builds are supported: firmware 57.0.8.0 (p2pcam sha256 5e2dc1a15734e214..., the author's camera) and
# 57.1.2.0 (146e7faff2f2dfd0..., ported from a user's dump, see patch_p2pcam.py). Any other build is refused:
# offsets differ between firmware versions and a wrong patch can boot-loop the camera.
# Needs: python3, unsquashfs and mksquashfs (squashfs-tools 4.x with xz).
set -euo pipefail
die(){ echo "error: $*" >&2; exit 1; }
[ $# -ge 1 ] || die "usage: $0 <stock p2pcam.sqfs> [outdir]"
IN=$1; OUT=${2:-out}
HERE=$(cd "$(dirname "$0")" && pwd)
for t in python3 unsquashfs mksquashfs; do command -v $t >/dev/null || die "$t not found"; done
md5(){ if command -v md5sum >/dev/null; then md5sum "$1" | cut -d' ' -f1; else md5 -q "$1"; fi; }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
unsquashfs -q -d "$T/root" "$IN" >/dev/null || die "cannot unpack $IN"
[ -f "$T/root/p2pcam" ] || die "no p2pcam inside $IN"

python3 -I "$HERE/patch_p2pcam.py" "$T/root/p2pcam" "$T/a" > "$T/patch.log"
python3 -I "$HERE/patch_rc.py" "$T/a" "$T/b" C2 >> "$T/patch.log"
cp "$T/b" "$T/root/p2pcam"; chmod 777 "$T/root/p2pcam"

mkdir -p "$OUT"
mksquashfs "$T/root" "$OUT/p2pcam.sqfs" -comp xz -b 131072 -noappend -no-xattrs \
  -force-uid 1000 -force-gid 1000 >/dev/null
md5 "$T/b" > "$OUT/p2pcam.md5"
md5 "$OUT/p2pcam.sqfs" > "$OUT/p2pcam.sqfs.md5"
cp "$T/patch.log" "$OUT/patch.log"
echo "patched p2pcam md5: $(cat "$OUT/p2pcam.md5")"
echo "  reference: 172468cf2bc865fb614e6ab55f8369b1 (fw 57.0.8.0), 0f93e2034ed5376f0c342c134895d1d2 (fw 57.1.2.0)"
echo "written to $OUT/ - copy p2pcam.sqfs, p2pcam.sqfs.md5 and p2pcam.md5 to <SD>/clean/"
