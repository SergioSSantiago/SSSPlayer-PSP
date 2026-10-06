# SSSPlayer for PSP Go

Music and media player for **PlayStation Portable Go** (and classic PSP), by SergioSSantiago.

Sister project of [SSSPlayer for PS Vita](https://github.com/SergioSSantiago/SSSPlayer). Same idea, different console: a focused player that reaches your media on the device storage, plays audio (and later video), and stays usable on the PSP’s limited hardware.

> **Status:** 0.2.6 plays MP3 and H.264/MP4 video with AAC audio (≤480×272) on PSP and PSP Go (same `EBOOT.PBP`).

## Why this port

- Play music from **Memory Stick (`ms0:`)** and, on PSP Go, from **internal storage (`ef0:`)**.
- Seek to the second you want, not just “next track”.
- Keep playback simple and reliable on custom firmware.
- Grow toward video and network features only where the PSP can do it well.

## Target platforms

| Device | Notes |
|--------|--------|
| **PSP Go** | Internal storage is `ef0:`. Memory Stick Micro, if inserted, is `ms0:` |
| PSP-1000 / 2000 / 3000 | Memory Stick is `ms0:`. Same EBOOT when CFW is present |
| PPSSPP | Development / testing on PC |

Requires custom firmware (or PPSSPP) to run homebrew.

## Planned features (MVP → later)

1. Browse `ef0:` / `ms0:`, opening `MUSIC` or `MP3` when that folder exists
2. Play MP3 (hardware decoder) with pause, seek, and next/previous
3. H.264/MP4 video via Media Engine with AAC audio (≤480×272)
4. Later: M3U, OGG

## Controls

The accept button follows the system X/O setting.

| Button | Browser | Now playing |
|--------|---------|-------------|
| Accept (X or O) | Open folder or play MP3 | Pause |
| Back | Parent folder | Stop and return |
| Up / Down | Move | |
| Left / Right | Seek −10s / +10s | Seek −10s / +10s |
| L / R | Previous / next track | Previous / next track |
| Triangle | Now playing | Back to files |
| Select | Pause | Pause |
| Square | LCD off (music keeps playing) | LCD off |
| Start | Quit | Quit |

The browser opens on the storage list, the same idea as the Vita file screen: **Internal (PSP Go)** and **Memory Stick**. Open one, then open `MUSIC` or `VIDEO`. Put MP3s in `ef0:/MUSIC` or `ms0:/MUSIC`. The header shows the build version (e.g. `Files  0.2.6`) so you can confirm the installed EBOOT.

### Video (H.264 / MP4)

Open an `.mp4` (or `.m4v`) with **H.264** video (≤480×272) and **AAC** audio. If the file cannot play, the app shows the real error (not an “MP3 only” stub). Square pauses; O / X stops.

YouTube / heavy network features from the Vita app are **out of scope** for early PSP builds.

## Build

Needs [PSPDEV / PSPSDK](https://github.com/pspdev/pspdev) (`psp-gcc`, `psp-config`).

```bash
export PSPDEV="$HOME/pspdev"   # or your install path
export PATH="$PSPDEV/bin:$PATH"
make
```

`make` also checks `~/pspdev` when `psp-config` is not on `PATH` yet.

Output: `EBOOT.PBP` in the project directory.

## Install

Copy `EBOOT.PBP` into a `SSSPlayer` folder on the memory you want to launch from:

- PSP Go internal: `ef0:/PSP/GAME/SSSPlayer/EBOOT.PBP`
- Memory Stick (classic PSP, or M2 on PSP Go): `ms0:/PSP/GAME/SSSPlayer/EBOOT.PBP`

Launch it from the XMB Games menu under CFW. The same file runs on PSP-1000/2000/3000 and PSP Go.

### Listen with the PSP Go screen closed

While a track is playing or paused, SSSPlayer:

- Blocks **auto-suspend** and locks Power-button sleep (`scePowerLock`) so the console stays on and music continues
- Turns the **LCD and backlight fully off** (brightness 0 + `sceDisplayDisable`) when you **close the Go slide**, or when **HOLD** / **Square** is used — not after idle time
- Opening the slide (or releasing HOLD / Square again) turns the LCD back on

Typical flow: start a track → close the panel → LCD off, music keeps playing. Open the panel to see the UI again.

If closing the slide still sleeps the console, set **Settings → System Settings → Display Panel Close Options → Standard**. If the LCD stays lit with Standard, try **Enter Sleep Mode** instead: PowerLock keeps the console awake while the lid event turns the LCD off.

## Relation to Vita SSSPlayer

| | Vita | PSP Go |
|--|------|--------|
| Repo | [SSSPlayer](https://github.com/SergioSSantiago/SSSPlayer) | this repo |
| Package | VPK | EBOOT.PBP |
| Storage | `ux0:` / `uma0:` | `ms0:` and `ef0:` (PSP Go internal) |
| Video | HW H.264 (Vita) | PSP Media Engine / software as needed |

Shared product goals; **separate codebases** (different SDKs).

## License

GPL-3.0-only. See `LICENSE`.

## Credits

SergioSSantiago. Inspired by the Vita SSSPlayer line and classic PSP homebrew media players.
