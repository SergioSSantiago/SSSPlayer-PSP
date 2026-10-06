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
- [x] Sleep / power: block auto-suspend + PowerLock while playing; LCD off via idle/HOLD/Square

## M3 — media later

- [x] Local H.264/MP4 via Media Engine NAL path (≤480×272)
- [x] AAC audio while video plays
- [ ] Optional network only if stable on Wi‑Fi PSP Go
