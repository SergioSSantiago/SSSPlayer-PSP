# SSSPlayer for PSP Go

Music and media player for **PlayStation Portable Go** (and classic PSP), by SergioSSantiago.

Sister project of [SSSPlayer for PS Vita](https://github.com/SergioSSantiago/SSSPlayer). Same idea, different console: a focused player that reaches your media on the device storage, plays audio (and later video), and stays usable on the PSP’s limited hardware.

> **Status:** early scaffold. Toolchain and first playable builds come next.

## Why this port

- Play music from **`ms0:`** (PSP Go internal flash / Memory Stick on PSP-1000/2000/3000).
- Seek to the second you want, not just “next track”.
- Keep playback simple and reliable on custom firmware.
- Grow toward video and network features only where the PSP can do it well.

## Target platforms

| Device | Notes |
|--------|--------|
| **PSP Go** | Primary target (`ms0:` internal storage) |
| PSP-1000 / 2000 / 3000 | Same EBOOT when CFW is present |
| PPSSPP | Development / testing on PC |

Requires custom firmware (or PPSSPP) to run homebrew.

## Planned features (MVP → later)

1. Browse `ms0:/MUSIC` (and configurable folders)
2. Play MP3 (then OGG / other formats the stack supports)
3. Now Playing with seek / pause / next-prev
4. Simple playlists (M3U)
5. Later: local video where Media Engine allows, optional network

YouTube / heavy network features from the Vita app are **out of scope** for early PSP builds.

## Build

Needs [PSPDEV / PSPSDK](https://github.com/pspdev/pspdev) (`psp-gcc`, `psp-config`).

```bash
export PSPDEV=/usr/local/pspdev   # or your install path
export PATH="$PSPDEV/bin:$PATH"
make
```

Output: `SSSPlayer.prx` / packaged `EBOOT.PBP` (see Makefile once the toolchain is wired).

## Install (when a release exists)

1. Download the release ZIP / EBOOT.
2. Copy to `ms0:/PSP/GAME/SSSPlayer/` (or `ef0:` on some setups).
3. Launch from the XMB Games menu under CFW.

## Relation to Vita SSSPlayer

| | Vita | PSP Go |
|--|------|--------|
| Repo | [SSSPlayer](https://github.com/SergioSSantiago/SSSPlayer) | this repo |
| Package | VPK | EBOOT.PBP |
| Storage | `ux0:` / `uma0:` | `ms0:` (+ `ef0:` where used) |
| Video | HW H.264 (Vita) | PSP Media Engine / software as needed |

Shared product goals; **separate codebases** (different SDKs).

## License

GPL-3.0-only. See `LICENSE`.

## Credits

SergioSSantiago. Inspired by the Vita SSSPlayer line and classic PSP homebrew media players.
