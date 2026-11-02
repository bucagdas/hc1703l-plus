# HC1703L internals (reverse-engineering notes)

[Türkçe](IC-YAPI.md)

Notes behind HC1703L Plus. Everything here was found on one camera with the `p2pcam` build whose sha256
starts with `5e2dc1a15734e214`. Addresses are virtual addresses in that binary (file offset = VA − 0x10000).
"Verified" means measured on the running camera; everything else is static analysis.

## Platform

- Augentix HC1703 family SoC, Cortex-A7, 64 MB RAM, 8 MB SPI NOR, Linux 3.18.31, uClibc, BusyBox 1.33.
- The kernel has no netfilter (no iptables), no `AF_PACKET`. Cloud blocking therefore uses routes.
- Sensor SC1346, **1280×720**. Stock firmware runs it at 720p and upscales to 1920×1080
  (`sensorFake=CHAZHI`, forced through `support_chazhi`). Verified: native 720p is sharper per pixel.
- `p2pcam` (stripped ARM Thumb, ET_EXEC, base 0x10000) statically contains the Augentix MPI (ISP,
  encoder, OSD, motion detection), the Tange cloud SDK and CS2 PPCS.

## Boot

1. Stock `start.sh` mounts the SD card on `/mnt` and runs `/mnt/debug_cmd.sh` if present (root, before
   Wi-Fi and before `p2pcam`, `/bak` writable at that moment).
2. `/bak/sensor.sh` loads the ISP/sensor files; the base hack adds a line that runs `/home/minihack.sh`
   only while `/mnt/debug_cmd.sh` does not exist.
3. `start.sh` unmounts `/mnt`, loop-mounts `/bak/p2pcam.sqfs` on `/p2pcam` and starts `p2pcam`, which
   remounts the card on `/tmp/mnt/mmc01/0`.
4. A 30 s hardware watchdog is fed only by `p2pcam`. When `p2pcam` exits, `start.sh` runs `reboot -f`.

Plus binds RAM copies over `/bak/p2pcam.sqfs` and `/bak/sensor.sh` from step 1, so flash is never written.

## p2pcam patches (`patcher/patch_p2pcam.py`)

| # | Where | What |
|---|---|---|
| 1 | `0x1a794` | stop forcing `support_chazhi=1` → native 720p |
| 2 | strings | HTTPDNS 203.107.1.x, log upload, 4G speed test, `ep.tange365.com`, `fidistr*`, AGPS → loopback |
| 3 | `0x32a263` | `teltun` cloud command (reverse telnet tunnel) renamed so it can never match |
| 4 | strings | cloud / port 6670 paths that start or kill `telnetd` → `true` |
| 5 | `0x5f840`… | every frame-rate site 12.5 → 25 (VFP imm), device fps 13 → 26, encoder `frm_rate_o` 12 → 25, GOP 25 → 50 |
| 6 | `0x601a2` | 720p main-stream VBR cap 1024 → 2048 kbps (raised to 2560 by `patch_rc.py`) |
| 7, 8 | `0x5f83e`, `0x5f8ca`, `0x5f748` | 25 FPS only in day mode; night / white light 8 → 12.5 |
| 9 | `0x16be4` | Tange cloud SDK never started (`main` falls into its `pause()` loop) |
| 10 | `0x583ac` | unauthenticated LAN firmware receiver (`do_update`) disabled |
| 11 | `0x113a84` + 32 call sites | every `bind()` goes through `bind_lo`: vendor servers bind to 127.0.0.1, except RTSP 554, DHCP 68 and the RTP/UDP sender |
| 12 | `0x55a4a` | `SO_SNDBUF` was set on the CTP socket instead of the RTSP client socket |
| 13 | `0x113c00` + 2 call sites | `req_gate`: port 554 shares a dispatcher with CTP/HTTP; non-RTSP requests are accepted only from loopback |
| 14 | `0x5ef24` | per-frame 300 KiB `memset` in `get_venc_stream` removed (CPU) |
| 15 | `0x19bae` | `support_ymv` default 1 → 0: vertical tracking |

`bind_lo` and `req_gate` live in the Tange command-handler area, which is dead code once patch 9 is in.
The local vendor API (`http://127.0.0.1:8001/...`, used by `httpclt`, the panel and `mo.sh`) keeps working.

## Rate control (`patcher/patch_rc.py`, set C2)

`T = max_bit_rate × 1024 × factor / 100`; the frame rate only divides the per-frame share. C2 sets the
factor to 100 in the day / night / white-light `g_rc_param` tables, `m2s` 28, texture 400000/21, the day
`g_rgl_qp` I/P ceiling 4/6 → 2/2, and the cap to 2560 kbps. Only index 5 of `g_vbr_qp_table` is read.

## ISP files

- `/bak/sensors/sensor_day_<sensor>.ini.lzma` is unpacked to `/tmp/sensor_day.ini` on every boot.
  `p2pcam` reloads it on day/night transitions, so a change can be tested in RAM with
  `ircut?mode=night` → `ircut?mode=day` → `ircut?mode=auto` (IR filter clicks, picture briefly B/W).
- `[AE] frame_rate` and `slow_frame_rate` **must match**. With 25 / 12.5, AE drops to 12.5 FPS in dim
  light and recordings (and RTSP) skip a second every ~4 s while another second lasts two. Verified;
  25 / 25 fixes it.
- `[SHP_V2] strength` scales detail and grain by the same factor (140 vs 170: both about −20 %, verified).
  140 is the stock value.
- `vb.ko param_vb_size_mb`: 27 MB stock, 18 MB is enough at native 720p (peak `/dev/vbs` use 13.7 MB),
  freeing about 11 MB of RAM.

## RTSP

- Port 554, paths `/0/av0` (main, H.265 + G.711 A-law audio) and `/0/av1` (sub). No authentication
  (`no_rtsp_auth`).
- Corrupted first frames on slow Wi-Fi come from the transport: `sendmsg` partial writes are not checked
  and `SO_SNDTIMEO` is 50 ms. Clips on the SD card are clean. With all patches applied, a six-boot
  first-connection test showed no corrupted frames (patch 12 was not tested in isolation).
- `http://127.0.0.1:8001/snapshot` returns a fresh 640×360 JPEG on the patched build (verified with three
  consecutive snapshots).

## Settings files (`/home`, flash)

`/home/devParam.dat` (1004 bytes) and `/home/extraParam.dat` (5120 bytes):

```
0x00 u32 magic 0x20150910
0x04 u32 checksum   sum of little-endian u16 words of the payload (+ odd trailing byte)
0x08 u32 length     payload length (992 / 5108)
0x0c ...            payload; p2pcam reads the whole file, so field offsets are file offsets
```

On a bad magic, length or checksum, `p2pcam` deletes the file and restores `*_bak`. After every save it
copies the file to `*_bak`. `devParam.dat` also holds the Wi-Fi SSID (+0x100) and password (+0x12c) in
plain text.

## Motion detection and alarm zones

| Setting | Where | Notes |
|---|---|---|
| Alarm zones | `devParam.dat` 0x314 | i32 count (max 25) + 25 × {u8 x, y, w, h} in tenths of the frame (10×10 grid). Read once when the event thread starts (`checkMdAreparams`, `0x16dc8`). Verified live |
| Zone support | `support_mdzone` | 1 at runtime on this camera (`0x3a4ca8`, read from process memory) although neither `hwcfg.ini` nor a `defalut.config` sets it; what sets it is unknown |
| Real threshold | `[config] motion_level` in `hwcfg.ini`, 0/1/2 | applied once at boot by `0x617bc`: moving blocks needed 15 / 4 / 1 (`md_setting_low/middle/high` override). Missing = 1. Verified live: `motion_level = 2` gives threshold 1 |
| Level shown to the app | `devParam.dat` 0xe8 = 1/2/3 | only read back for the vendor app |
| Motion tracking | `devParam.dat` 0x2d8 | while on, recording skips the zone check; see Tracking below |
| Event switches | `devParam.dat` 0x388 motion, 0x38c person, 0x390 sound | 2 = off (static analysis) |
| Defence, zone state | `devParam.dat` 0x3d4, 0x3d5 | |
| App event flags | `extraParam.dat` 0x1340–0x134d | `setMotEventStatus` 0x1346 (2 = no recording trigger), `setMotionSensity` 0x134b, `setAreaSensity` 0x1349, … |

`FUN_00018fb8` reads keys from `[defalut]` in `/home/defalut.config` or `/bak/defalut.config`, but neither file
exists on this camera, so the built-in defaults apply. `hwcfg.ini` (`[config]`) is read from
`/home/hwcfg.ini` if present, else `/bak/hwcfg.ini`. `start.sh` moves a `/home/hwcfg.ini` to `/bak` on the next
boot, which is a flash write; Plus avoids that with a bind mount.

How a clip starts: the event thread (`0x340e4`) receives motion events; with `support_mdzone` on, an event that
does not overlap a zone is dropped (`0x33a24`) unless tracking is on. A surviving event calls `FUN_00033218(1)`,
which starts a 30 s event recording (`FUN_001d5a9e(0,2,0x1e)`). Zone count −1 means the whole frame.

Walk test with one zone: a person moving outside the zone for minutes produced no clip, and a clip started within
seconds of entering it, twice. Before entering, not a single event had reached `0x33a24` (its first-call flag
`0x3a86a4` stayed set), so outside motion may be filtered earlier than the code above. The exact place is unknown.

At runtime only the Tange command handler (`TCI_CMD_SETMOTIONDETECT`, `SET_MOTION_TRACKER_REQ`,
`SET_MDAREA_STATE`, `SET_DEFENCE_REQ`) changes these, and it is disabled here. Plus therefore applies them at
boot, before `p2pcam` starts: `debug_cmd.sh` runs `clean/bin/mdcfg` (source `tools/mdcfg.c`) to build a RAM copy
of `devParam.dat` from `clean/md.conf` and bind-mounts it over `/home/devParam.dat`, and does the same for
`/bak/hwcfg.ini`. Flash stays untouched (verified by comparing md5 sums through a raw bind of `/home` and `/bak`).

`mdcfg live` also writes sensitivity (`0x3f1b0c`) and tracking (devParam RAM copy + 0x2d8) to the running `p2pcam`
through `/proc/<pid>/mem`, RAM only. It checks a string in the read-only data and the devParam magic first, and the
panel also compares the running binary with `clean/p2pcam.md5`. Tracking was seen working after a live write.
Zones stay boot-only: with zones written to `0x3a8514`, count to `0x39ddf8` and flag `0x3a86a4` set, motion started
clips and even a camera move did not clear the flag, so `0x33a24` was not reached.

Runtime values can be read without changing anything:
`dd if=/proc/<pid>/mem bs=4 skip=$((0x3a4ca8/4)) count=1`. Useful addresses in this build: `0x3a4ca8`
`support_mdzone`, `0x3a4bf4` `motion_level`, `0x3f1b0c` threshold, `0x39ddf8` zone count, `0x3a8514` zones.

Debug switches: `touch /tmp/md_info`, `/tmp/md_region`, `/tmp/track_info`, `/tmp/od_info` or
`/tmp/hd_info` makes `p2pcam` print motion, zone, tracking and person-detection details (not tested yet).

### Tracking

`/dev/ptz` takes three ioctls with an int array: 0x81 range (510 × 160), 0x82 position, 0x83 go to position.
Numbers below were measured on the camera unless marked static.

- **Dead zone.** `FUN_00060e68` (static) clears dx and dy while the object's centre is in the middle half of the
  frame. Every measured step sat at or beyond that limit; a step value of 21 is exactly 25 % of the width.
- **Zones.** Tracking starts only for an event that overlaps a zone (`0x33a24`, static). With a zone on the right
  all nine steps went right; with the whole frame they went both ways; with a zone at 20–50 % the camera did not
  move at all. Recording does not wait for this: with tracking on, a clip started for motion outside the zone.
- **Tilt.** `support_ymv` (default 1, from the absent `defalut.config`) clears dy in `FUN_00033978`. Patch 15 makes
  the default 0. Tilt then moves only with a pan, because of the dead zone, and `FUN_000399e4` multiplies its step
  by 7.
- **Return.** Ten seconds after the last step, `FUN_0003990c` goes back to the position read when tracking started.
- **Re-reference.** Every 10th return (`ptzAdjustCount`) and 30 minutes after a return, it calls ioctl 0x8a instead:
  the camera sweeps and lands on the position in `/home/psp.dat`. The argument is `support_ymv != 0`; 1 sweeps pan
  only, 0 sweeps both axes (measured, about 20 s).
- **`/home/psp.dat`** is 20 bytes: `PSPT`, then x, y, z and x + y − 176 as i32. The driver rewrites it after a move,
  and after each tracking return, so tracking adds flash writes. It is read at boot without moving the motors. A
  drifted value therefore stays hidden until the first re-reference: on our camera that turned the view 57 steps
  away from the door, and a reboot did not bring it back. Moves made with the panel arrows update the file while
  tracking is off; during tracking saving is off (ioctl 0x89).
- A reboot during tracking leaves the camera away from the saved position until the next re-reference (static).

Saved positions live in `clean/ptz.conf` on the SD card. The gears have about 5 steps of play, so `tools/ptzpos.c`
always takes the last 12 steps from the same side; from all four directions it landed within 1 of 320 pixels. The
same position reached by the re-reference above can differ by about 4 tilt steps, because that sweep arrives from
the other side.

The selected image mode is the byte at devParam + 0xfa (0 auto, 1 day, 2 night), and p2pcam keeps it in the file.
The panel reads it with `mdcfg image`; the running mode alone cannot show Auto.

## Cloud endpoints

`netguard.sh` covers `/home/etc/resolv.conf` (the target of `/etc/resolv.conf`, on flash) with a read-only RAM
copy. `p2pcam` rewrites that file at boot; before this, DNS pointed at the router for up to 5 s and the file was
rewritten on flash on every boot.

Seen in the binary: Alibaba HTTPDNS `203.107.1.1/.33/.34/.35`, `139.224.31.65` (log upload),
`47.107.191.43` (4G speed test), `ep.tange365.com`, `fidistr.tange365.com`, `www.goke-agps.com`.
`hosts` blocking is not enough because some of them are IP literals. On the author's camera an active test
showed 100 % loss to public addresses; a router rule was in place at the same time, so the camera's routes
were not measured on their own.
