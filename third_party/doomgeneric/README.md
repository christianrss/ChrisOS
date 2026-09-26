This tree is the ChrisOS bring-up of doomgeneric (GPLv2).

Vendored upstream snapshot: `third_party/doomgeneric_src` (clone of https://github.com/ozkl/doomgeneric).

## Playable path (ENGINE)

- `GAMES/DOOM/ENGINE.LST` → `ENGINE.CLV` (full doomgeneric + Chris glue)
- `GAMES/DOOM/MAIN.CC` — argv `-iwad GAMES/DOOM/DOOM1.WAD -mb 16`
- `GAMES/DOOM/I_CHRIS.CC` — DG_* video/timer (320×200, `fb_blit` / `setpal`)
- `GAMES/DOOM/I_VIDEO.CC` / `I_INPUT.CC` / `I_SOUND.CC` / `W_FILE.CC` — Chocolate Doom I_* + WAD IO
- `GAMES/DOOM/DOOM1.WAD` — **Freedoom Phase 1** (`freedoom1.wad`). Do not commit commercial IWADs.
- Desktop / taskbar / shell launch `GAMES/DOOM/ENGINE.CLV`

Controls: arrows move, Ctrl fire, Space use, Esc menu, Enter select, Y/N quit confirm.

## Demo path (optional)

- `GAMES/DOOM/DOOM.LST` → `DOOM.CLV` — palette / fb_blit bring-up only
- `GAMES/DOOM/DOOM1.MINI.WAD` — PLAYPAL-only mini IWAD for tests

Do not compile Chocolate Doom + SDL here.
