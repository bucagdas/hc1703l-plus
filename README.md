# HC1703L Plus

[Türkçe](README.tr.md)

A modern, privacy-first layer for the cheap Augentix **HC1703L** pan/tilt camera, built on top of
[Jalecom's hack](https://github.com/Jalecom/AJ_HC1703L_Teardown). The vendor program `p2pcam` stays in place as a closed "driver": it is
patched from **your own** copy of the firmware, and everything you interact with runs from the SD card.

> **Status:** in daily use on the author's camera since October 2026. Tested on **one** camera and **one**
> firmware build. Read [Compatibility](#compatibility) and [Recovery](#recovery) before you start.

## What you get

| | Stock | Plus |
|---|---|---|
| Resolution | 1920×1080, upscaled from a 1280×720 sensor | native 1280×720 |
| Frame rate | day 12.5, night 8 | day 25, night 12.5 |
| Video bitrate cap | 1 Mbps | 2.56 Mbps |
| Free RAM | ~14 MB | ~22 MB |
| Cloud | Tange/iCam365 SDK, HTTPDNS, reverse `teltun` tunnel | SDK never started, endpoints → loopback, tunnel command broken, all public IPs rejected |
| p2pcam services on the LAN | 80, 6670, 8001, 20202, CTP/HTTP over 554 | RTSP on 554 only |

Plus also adds:

- **Control panel** on `:8080`: live preview, press-and-hold PTZ, white light / IR, day / night / auto,
  sharpness presets, status (patched or stock, cloud block, Wi-Fi, SD, RAM, CPU, last motion), logs.
  Commands are POST-only, so reloading the page or restoring a tab never re-runs a command. English and
  Turkish (EN/TR switch in the header; the gallery follows the panel's choice).
- **Recordings gallery** on `:8090`: motion events as cards (snapshot + clip), per-day view, in-browser
  playback, download, delete, delete a whole day, a motion e-mail switch for an external mailer.
- **Motion snapshots** (`mo.sh`): one snapshot per motion clip, linked to it.
- **Fixes:** RTSP socket buffer on the wrong socket, recordings skipping seconds in dim light, vendor
  port 554 accepting non-RTSP commands from the LAN.
- **Research:** where motion detection and alarm zones are stored, plus a tool to edit them
  ([INTERNALS](docs/INTERNALS.md)).

## How it works

```
power on
 └─ stock start.sh
     ├─ <SD>/debug_cmd.sh   (Plus loader, deletes itself = one-shot)
     │    ├─ netguard.sh    cloud block (RAM copy)
     │    ├─ boot.sh        waits for the SD card, then runs health.sh
     │    └─ binds RAM copies of clean/p2pcam.sqfs and clean/sensor.sh over /bak   (flash untouched)
     ├─ sensor.sh → minihack.sh (base hack: Wi-Fi, telnet, FTP, httpd)
     └─ p2pcam (patched)
health.sh  T+0 plus.sh (panel, gallery, motion service)
           T+5 min  healthy → put the loader back; 3 failed boots in a row → stays stock
           T+30 min still healthy → clear the failure counters
```

Plus never writes to flash. Removing the SD card, or deleting `debug_cmd.sh` from it, gives you the plain
base hack on the next boot.

## Compatibility

- Only the `p2pcam` build with sha256 `5e2dc1a15734e214…` (sensor SC1346). `build.sh` refuses anything
  else: patch offsets differ between builds and a wrong patch can boot-loop the camera.
- Requires the base hack's **permanent** version (`HACKTYPE=P`, `/home/minihack.sh`) for Wi-Fi without the
  cloud, telnet, FTP and the BusyBox httpd.
- Build host: Linux or macOS with `python3` and `squashfs-tools` (`unsquashfs`, `mksquashfs`, xz).

## Install

1. Install the base hack's permanent version as described in [Jalecom's README](https://github.com/Jalecom/AJ_HC1703L_Teardown).
   Check that telnet, FTP and `http://<camera>:8080` work.
2. Copy `/bak/p2pcam.sqfs` from the camera to your computer (FTP).
3. Build: `patcher/build.sh p2pcam.sqfs out/`. The patched `p2pcam` md5 must be
   `a34010b654e0b9bd53a2a231f228bd3b`.
4. Copy to the SD card root: `sdcard/clean/`, `sdcard/motion/`, `sdcard/mo.sh`.
   Put `out/p2pcam.sqfs`, `out/p2pcam.sqfs.md5` and `out/p2pcam.md5` into `clean/`.
   **Do not copy `debug_cmd.sh` yet.**
5. Boot the camera, then over telnet: `sh /tmp/mnt/mmc01/0/clean/prepare.sh`
   It builds `sensor.sh` and `sensor_day.ini` from your camera's own files (no vendor files are shipped).
6. Arm the loader: `cp /tmp/mnt/mmc01/0/clean/debug_cmd.sh /tmp/mnt/mmc01/0/; sync; reboot`
7. After about a minute the panel's status should read **Patched**, and the RTSP stream
   (`rtsp://<camera>:554/0/av0`) should be 1280×720.

## Recovery

- **Back to stock for one boot:** delete `debug_cmd.sh` from the SD card root. The next healthy boot
  re-arms it.
- **Disable Plus:** `touch <SD>/clean/DISABLE` and delete `debug_cmd.sh` from the root.
- **Camera unreachable or rebooting in a loop:** power off, put the SD card in a computer, delete
  `debug_cmd.sh` from the card root, put it back, power on. Why it can happen: the base hack's line in
  `/bak/sensor.sh` skips `minihack.sh` while `debug_cmd.sh` exists, and `minihack.sh` is what brings Wi-Fi
  up. If the loader cannot run or delete itself, there is no Wi-Fi and the camera reboots in a loop
  (observed once; most likely `p2pcam`'s own reboot when the Wi-Fi interface stays down). If a Mac shows
  the card as read-only, check the lock switch on the SD adapter.

## Security notes

- Keep the camera on a private network and never expose it to the internet.
- RTSP has no password (stock behaviour) and includes audio.
- Telnet (no password) and FTP (`root` / `cxlinux`) come from the base hack: anyone on your LAN can
  control the camera.
- The panel and gallery have no login, by design, for a private network.
- The base hack ships one shared dropbear host key. If you enable SSH, generate your own key.

## Known limitations

- One firmware build only. Other builds need new offsets.
- Tested on one camera.
- Motion zones: offline tool only, not in the panel yet. Zones are ignored unless `hwcfg.ini` has
  `support_mdzone=1` (see [INTERNALS](docs/INTERNALS.md)).
- No HTTPS (BusyBox httpd), so the panel cannot use the browser microphone for two-way audio.

## Credits and license

- [@Jalecom](https://github.com/Jalecom) for the [base hack and teardown](https://github.com/Jalecom/AJ_HC1703L_Teardown), which this project
  builds on and requires, and the contributors listed there.
- mp4box.js 0.5.2 by GPAC (BSD 3-Clause) is included for in-browser playback.
- This repository contains only HC1703L Plus code, MIT licensed ([LICENSE](LICENSE)). The base hack is
  not included; get it from its own repository.
- Not affiliated with Augentix or Tange. No vendor binaries are distributed. Use at your own risk.

## Optional: run the health check on every boot

Without this, the health check only runs on boots where the loader was armed. To also run it after a
fallback boot, add this to the end of `/home/minihack.sh` (a flash write; keep a copy of the original):

```sh
( n=0; until grep -q ' /tmp/mnt/mmc01/0 ' /proc/mounts || [ $n -ge 90 ]; do sleep 2; n=$((n+1)); done
  [ -f /tmp/mnt/mmc01/0/clean/health.sh ] && cp /tmp/mnt/mmc01/0/clean/health.sh /tmp/health.sh && sh /tmp/health.sh ) </dev/null >/dev/null 2>&1 &
```
