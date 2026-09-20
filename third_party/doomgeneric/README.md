This tree is the ChrisOS bring-up of doomgeneric (GPLv2).

Vendored upstream snapshot: `third_party/doomgeneric_src` (clone of https://github.com/ozkl/doomgeneric).

The playable ChrisC path:

- `GAMES/DOOM/I_CHRIS.CC` — DG_Init / DG_DrawFrame / DG_GetKey / DG_SleepMs / DG_GetTicksMs (sound/net stub)
- `GAMES/DOOM/DOOM.CC` — doomgeneric_Create / Tick, 320x200 paletted fb_blit, WAD PLAYPAL
- `GAMES/DOOM/DOOM.LST` — `cc GAMES/DOOM/DOOM.LST`
- `GAMES/DOOM/DOOM1.WAD` — mini IWAD. Replace with Freedoom/shareware on CFS.

Do not compile Chocolate Doom + SDL here.
