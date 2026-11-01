#!/usr/bin/env python3
"""HC1703L Plus - read and edit the motion-detection fields of /home/devParam.dat. MIT License.

The camera keeps its settings in two flash files with the same layout:
    0x00 u32 magic 0x20150910
    0x04 u32 checksum: sum of little-endian u16 words of the payload (odd trailing byte added)
    0x08 u32 payload length (devParam: 992, file 1004 bytes)
    0x0c ... payload; p2pcam reads the whole file into RAM, so field offsets below are FILE offsets.

Motion fields found by reverse engineering (see ../docs/INTERNALS.md):
    0x0e8 u8   sensitivity level reported to the vendor app (1 low, 2 middle, 3 high)
    0x2d8 i32  motion tracking (PTZ follows motion)
    0x314 i32  number of alarm zones (max 25), followed by 25 x {u8 x, y, w, h} in tenths of the frame
    0x3d4 u8   defence (armed)
    0x3d5 u8   alarm-zone state

Zones are only used when hwcfg.ini has support_mdzone=1 (missing = 0). The real detection threshold comes
from hwcfg.ini motion_level at boot, not from 0xe8. p2pcam keeps a RAM copy and rewrites the file whenever a
setting changes, so an edited file must be installed and followed by an immediate reboot.

This tool never prints the WiFi credentials stored in the same file.

EXPERIMENTAL: writing an edited devParam.dat to the camera has not been tested yet. If the checksum is
wrong, p2pcam deletes the file and restores /home/devParam.dat_bak.

Usage:
    devparam.py show devParam.dat
    devparam.py zones devParam.dat -o new.dat --set "0,0,5,10;5,5,5,5"
    devparam.py zones devParam.dat -o new.dat --clear
"""
import argparse
import struct
import sys

MAGIC = 0x20150910
ZONE_OFF, ZONE_MAX = 0x314, 25


def checksum(payload: bytes) -> int:
    s = 0
    for i in range(0, len(payload) - 1, 2):
        s += struct.unpack_from('<H', payload, i)[0]
    if len(payload) % 2:
        s += payload[-1]
    return s & 0xFFFFFFFF


def load(path: str) -> bytearray:
    b = bytearray(open(path, 'rb').read())
    if len(b) < 12:
        sys.exit('file too short')
    magic, chk, length = struct.unpack_from('<III', b, 0)
    if magic != MAGIC:
        sys.exit(f'bad magic {magic:#x}')
    if length != len(b) - 12:
        sys.exit(f'length field {length} does not match file size {len(b)}')
    if checksum(b[12:]) != chk:
        sys.exit('checksum mismatch: file is corrupt')
    return b


def save(b: bytearray, path: str) -> None:
    struct.pack_into('<I', b, 4, checksum(b[12:]))
    open(path, 'wb').write(b)


def zones(b: bytes):
    n = struct.unpack_from('<i', b, ZONE_OFF)[0]
    out = []
    for i in range(max(0, min(n, ZONE_MAX))):
        out.append(tuple(b[ZONE_OFF + 4 + 4 * i: ZONE_OFF + 8 + 4 * i]))
    return n, out


def cmd_show(a):
    b = load(a.file)
    if len(b) != 1004:
        print('note: this does not look like devParam.dat (expected 1004 bytes)')
    print(f'checksum ok, {len(b)} bytes')
    print(f'sensitivity level reported to app (0xe8): {b[0xe8]}')
    print(f'motion tracking (0x2d8): {struct.unpack_from("<i", b, 0x2d8)[0]}')
    print(f'defence / armed (0x3d4): {b[0x3d4]}')
    print(f'alarm-zone state (0x3d5): {b[0x3d5]}')
    n, zs = zones(b)
    print(f'alarm zones (0x314): {n}')
    for i, (x, y, w, h) in enumerate(zs):
        print(f'  zone {i}: x={x} y={y} w={w} h={h}  -> {x*10}-{(x+w)*10}% across, {y*10}-{(y+h)*10}% down')


def cmd_zones(a):
    b = load(a.file)
    if a.clear:
        zs = []
    else:
        zs = []
        for part in (a.set or '').split(';'):
            part = part.strip()
            if not part:
                continue
            v = [int(x) for x in part.split(',')]
            if len(v) != 4:
                sys.exit(f'zone needs x,y,w,h: {part}')
            x, y, w, h = v
            if not (0 <= x < 10 and 0 <= y < 10 and 1 <= w <= 10 - x and 1 <= h <= 10 - y):
                sys.exit(f'zone outside the 10x10 grid: {part}')
            zs.append((x, y, w, h))
        if not zs:
            sys.exit('give --set "x,y,w,h;..." or --clear')
    if len(zs) > ZONE_MAX:
        sys.exit(f'at most {ZONE_MAX} zones')
    b[ZONE_OFF:ZONE_OFF + 4 + 4 * ZONE_MAX] = bytes(4 + 4 * ZONE_MAX)
    struct.pack_into('<i', b, ZONE_OFF, len(zs))
    for i, z in enumerate(zs):
        b[ZONE_OFF + 4 + 4 * i: ZONE_OFF + 8 + 4 * i] = bytes(z)
    save(b, a.out)
    print(f'wrote {a.out} with {len(zs)} zone(s); install it, then reboot the camera immediately')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    s = p.add_subparsers(dest='cmd', required=True)
    sh = s.add_parser('show'); sh.add_argument('file'); sh.set_defaults(f=cmd_show)
    z = s.add_parser('zones'); z.add_argument('file'); z.add_argument('-o', '--out', required=True)
    z.add_argument('--set'); z.add_argument('--clear', action='store_true'); z.set_defaults(f=cmd_zones)
    a = p.parse_args()
    a.f(a)


if __name__ == '__main__':
    main()
