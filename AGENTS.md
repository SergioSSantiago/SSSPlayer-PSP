# SSSPlayer-PSP — agent notes

## Product

Port of the SSSPlayer *product idea* to PSP Go / PSP (CFW). Not a code fork of the Vita tree — different SDK (`pspdev`).

## Build

- Prefer `make` with PSPDEV on `PATH`.
- Do not invent Vita APIs here (`vita2d`, `sceVideodec`, etc.).
- First milestone after toolchain: directory browser + MP3 playback. Memory Stick is `ms0:` on PSP and PSP Go. PSP Go internal flash is `ef0:`. One EBOOT must see both.

## Scope guardrails

- PSP Go first; classic PSP OK if same EBOOT.
- No YouTube / VitaDB / Alive store wiring until core player works.
- Keep commits small; do not vendor huge binaries.
