import re
import os

builtins = {
    "pixel": 3, "rect": 5, "line": 5, "sprite": 6, "tilemap": 7, "clear": 1,
    "key": 1, "ticks": 0, "wait": 1, "tone": 2, "tri": 10, "mesh": 5,
    "transform": 3, "meshf": 8, "sin": 1, "cos": 1, "cam": 5, "light": 6,
    "tex": 1, "voxel": 4, "voxel_get": 3, "world": 0, "viewport": 2,
    "screen_w": 0, "screen_h": 0, "fps": 0, "fopen": 1, "fclose": 1,
    "fread": 3, "fwrite": 3, "fsize": 1, "fexists": 1, "malloc": 1, "free": 1,
    "realloc": 2, "setjmp": 1, "longjmp": 2, "gc_alloc": 1, "gc_collect": 0,
    "thrd_create": 2, "thrd_join": 1, "cla_load": 1, "fseek": 2, "fb_blit": 3,
    "setpal": 1, "mouse_x": 0, "mouse_y": 0, "mouse_btn": 0, "ev_key": 0,
    "ev_text": 0, "fillrgb": 5, "text": 4, "glyph": 4, "surf_place": 4,
    "surf_move": 2, "surf_raise": 0, "surf_close": 0, "readdir": 3, "mkdir": 1,
    "unlink": 1, "rename": 2, "app_spawn": 1, "app_kill": 1, "app_count": 0,
    "app_info": 2, "sys_cc": 1, "sys_run": 1, "sys_make": 2, "disp_w": 0,
    "disp_h": 0, "sys_err": 1, "app_spawn_arg": 2, "app_arg": 1, "lib_load": 1,
    "lib_reload": 1, "isdir": 1, "kb_layout": 1, "kb_get": 0,
}
special = {"fopen": set(range(1, 8)), "fread": {3, 4}, "fwrite": {3, 4}, "fseek": {2, 3}}
files = []
for r, ds, fs in os.walk("third_party/doomgeneric_src/doomgeneric"):
    for f in fs:
        if f.endswith((".c", ".h")):
            files.append(os.path.join(r, f))
for f in [
    "GAMES/DOOM/MAIN.CC", "GAMES/DOOM/I_CHRIS.CC", "GAMES/DOOM/I_VIDEO.CC",
    "GAMES/DOOM/I_SOUND.CC", "GAMES/DOOM/I_INPUT.CC", "GAMES/DOOM/W_FILE.CC",
    "LIB/STRING.CC", "LIB/STDIO.CC", "LIB/STDLIB.CC", "LIB/CTYPE.CC", "LIB/MATH.CC",
]:
    files.append(f)
call_re = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
issues = []
for path in files:
    try:
        text = open(path, encoding="utf-8", errors="ignore").read()
    except OSError:
        continue
    i = 0
    while True:
        m = call_re.search(text, i)
        if not m:
            break
        name = m.group(1)
        i = m.end()
        if name not in builtins:
            continue
        start = m.end() - 1
        depth = 0
        j = start
        commas = 0
        in_s = None
        while j < len(text):
            ch = text[j]
            if in_s:
                if ch == "\\":
                    j += 2
                    continue
                if ch == in_s:
                    in_s = None
                j += 1
                continue
            if ch in "\"'":
                in_s = ch
                j += 1
                continue
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    args = text[start + 1 : j].strip()
                    nargs = 0 if not args else commas + 1
                    exp = builtins[name]
                    if name in special:
                        ok = nargs in special[name]
                    else:
                        ok = nargs == exp
                    if not ok:
                        line = text.count("\n", 0, m.start()) + 1
                        snippet = text[m.start() : j + 1][:100].replace("\n", " ")
                        issues.append((path, line, name, nargs, exp, snippet))
                    break
            elif ch == "," and depth == 1:
                commas += 1
            j += 1
        else:
            break
for it in issues[:50]:
    print("%s:%d %s nargs=%s expected=%s :: %s" % (it[0], it[1], it[2], it[3], it[4], it[5]))
print("TOTAL", len(issues))
