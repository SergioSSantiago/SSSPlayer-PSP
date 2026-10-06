# Roadmap

## M0 — scaffold

- [x] Repo + Makefile + EBOOT bootstrap
- [x] `pspdev` build produces `EBOOT.PBP`
- [ ] Confirm the EBOOT on a PSP, a PSP Go, and PPSSPP

## M1 — audio MVP

- [x] Browse `ms0:` and `ef0:`, preferring `MUSIC` then `MP3`
- [x] MP3 decode (`sceMp3`) + `sceAudioSRC`
- [x] Now Playing: play / pause / seek / next

## M2 — polish

- [ ] M3U playlists
- [x] Simple UI theme (Terminus)
- [x] Sleep / power: block auto-suspend + PowerLock while playing; black screen via HOLD/Square

## M3 — media later

- [ ] Local video via Media Engine (`sceMpeg` / H.264) for MP4 and related
- [ ] Optional network only if stable on Wi‑Fi PSP Go
