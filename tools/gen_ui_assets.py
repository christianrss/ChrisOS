#!/usr/bin/env python3
"""Turn assets/*.png into raw RGBA blobs and a monospace 8x16 font.

The kernel has no PNG decoder. Icons land at 48x48. The wallpaper is scaled
so it fits inside 1920x1080 and the desktop can center it.
"""
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
OUT = os.path.join(ROOT, "build", "icons")
FONT_C = os.path.join(ROOT, "kernel", "gfx", "font.c")
ICONS_C = os.path.join(ROOT, "kernel", "gfx", "icons_tab.c")

ICON_NAMES = [
    ("shell", "ChrisOS_Shell.png"),
    ("files", "ChrisOS_FileManager.png"),
    ("editor", "ChrisOS_Editor.png"),
    ("tasks", "ChrisOS_TaskManager.png"),
    ("start", "ChrisOS_StartMenu.png"),
    ("logo", "ChrisOS_Monolito.png"),
    ("mine", "ChrisOS_MineChris.png"),
]


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a png: " + path)
    pos = 8
    w = h = 0
    depth = color = inter = 0
    idat = bytearray()
    plte = b""
    while pos + 8 <= len(data):
        ln, typ = struct.unpack(">I4s", data[pos:pos + 8])
        pos += 8
        chunk = data[pos:pos + ln]
        pos += ln + 4
        if typ == b"IHDR":
            w, h, depth, color, comp, filt, inter = struct.unpack(">IIBBBBB", chunk)
            if comp != 0 or filt != 0 or inter != 0 or depth != 8:
                raise SystemExit("unsupported png " + path)
            if color not in (2, 3, 6):
                raise SystemExit("unsupported color " + path)
        elif typ == b"PLTE":
            plte = chunk
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"IEND":
            break
    raw = zlib.decompress(bytes(idat))
    if color == 2:
        bpp = 3
    elif color == 6:
        bpp = 4
    else:
        bpp = 1
    stride = w * bpp
    rows = []
    i = 0
    prev = bytearray(stride)
    for y in range(h):
        filt = raw[i]
        i += 1
        row = bytearray(raw[i:i + stride])
        i += stride
        if filt == 1:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + left) & 255
        elif filt == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 255
        elif filt == 3:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + ((left + prev[x]) // 2)) & 255
        elif filt == 4:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                up = prev[x]
                ul = prev[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + paeth(left, up, ul)) & 255
        elif filt != 0:
            raise SystemExit("bad filter")
        prev = row
        rows.append(row)
    pix = bytearray(w * h * 4)
    for y in range(h):
        row = rows[y]
        for x in range(w):
            o = (y * w + x) * 4
            if color == 6:
                s = x * 4
                pix[o:o + 4] = row[s:s + 4]
            elif color == 2:
                s = x * 3
                pix[o] = row[s]
                pix[o + 1] = row[s + 1]
                pix[o + 2] = row[s + 2]
                pix[o + 3] = 255
            else:
                idx = row[x] * 3
                pix[o] = plte[idx]
                pix[o + 1] = plte[idx + 1]
                pix[o + 2] = plte[idx + 2]
                pix[o + 3] = 255
    return w, h, pix


def fit(sw, sh, max_w, max_h):
    if sw <= max_w and sh <= max_h:
        return sw, sh
    dw = max_w
    dh = max_h
    if sw * max_h < sh * max_w:
        dw = max(1, (sw * max_h) // sh)
    else:
        dh = max(1, (sh * max_w) // sw)
    return dw, dh


def downscale(pix, sw, sh, dw, dh):
    out = bytearray(dw * dh * 4)
    for y in range(dh):
        y0 = y * sh // dh
        y1 = (y + 1) * sh // dh
        if y1 <= y0:
            y1 = y0 + 1
        for x in range(dw):
            x0 = x * sw // dw
            x1 = (x + 1) * sw // dw
            if x1 <= x0:
                x1 = x0 + 1
            acc = [0, 0, 0, 0]
            n = 0
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    s = (yy * sw + xx) * 4
                    for c in range(4):
                        acc[c] += pix[s + c]
                    n += 1
            o = (y * dw + x) * 4
            for c in range(4):
                out[o + c] = acc[c] // n
    return out


def write_bin(path, rgba):
    raw = bytearray()
    n = len(rgba) // 4
    for i in range(n):
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        raw += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b)
    open(path, "wb").write(raw)


def font_from_pil():
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return None
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
        "C:/Windows/Fonts/consola.ttf",
    ]
    path = None
    for c in candidates:
        if os.path.isfile(c):
            path = c
            break
    if not path:
        return None
    face = ImageFont.truetype(path, 14)
    glyphs = []
    for code in range(32, 127):
        im = Image.new("L", (8, 16), 0)
        draw = ImageDraw.Draw(im)
        draw.text((0, 0), chr(code), font=face, fill=255)
        rows = []
        for y in range(16):
            bits = 0
            for x in range(8):
                if im.getpixel((x, y)) >= 128:
                    bits |= 1 << (7 - x)
            rows.append(bits)
        glyphs.append(rows)
    return glyphs


def font_fallback():
    """5-wide glyphs, doubled vertically into an 8x16 cell. Original bitmaps."""
    # Each entry is 8 rows of 5 bits, ink in the low 5, drawn in bits 6..2.
    raw = {
        " ": [],
        "!": [0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04],
        '"': [0x0A, 0x0A, 0x0A],
        "#": [0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A],
        "$": [0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04],
        "%": [0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13],
        "&": [0x08, 0x14, 0x14, 0x08, 0x15, 0x12, 0x0D],
        "'": [0x04, 0x04, 0x04],
        "(": [0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02],
        ")": [0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08],
        "*": [0x00, 0x04, 0x15, 0x0E, 0x15, 0x04],
        "+": [0x00, 0x04, 0x04, 0x1F, 0x04, 0x04],
        ",": [0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08],
        "-": [0x00, 0x00, 0x00, 0x1F, 0x00],
        ".": [0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C],
        "/": [0x01, 0x02, 0x04, 0x08, 0x10],
        "0": [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E],
        "1": [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E],
        "2": [0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F],
        "3": [0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E],
        "4": [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02],
        "5": [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
        "6": [0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E],
        "7": [0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08],
        "8": [0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E],
        "9": [0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C],
        ":": [0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C],
        ";": [0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08],
        "<": [0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02],
        "=": [0x00, 0x1F, 0x00, 0x1F],
        ">": [0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08],
        "?": [0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04],
        "@": [0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E],
        "A": [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
        "B": [0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E],
        "C": [0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E],
        "D": [0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C],
        "E": [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F],
        "F": [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10],
        "G": [0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F],
        "H": [0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
        "I": [0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E],
        "J": [0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C],
        "K": [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11],
        "L": [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F],
        "M": [0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11],
        "N": [0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11],
        "O": [0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
        "P": [0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10],
        "Q": [0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D],
        "R": [0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11],
        "S": [0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E],
        "T": [0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
        "U": [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
        "V": [0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04],
        "W": [0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A],
        "X": [0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11],
        "Y": [0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04],
        "Z": [0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F],
        "[": [0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E],
        "\\": [0x10, 0x08, 0x04, 0x02, 0x01],
        "]": [0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E],
        "^": [0x04, 0x0A, 0x11],
        "_": [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F],
        "`": [0x08, 0x04, 0x02],
        "{": [0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02],
        "|": [0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
        "}": [0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08],
        "~": [0x00, 0x00, 0x08, 0x15, 0x02],
    }
    lower = {
        "a": [0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F],
        "b": [0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E],
        "c": [0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E],
        "d": [0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F],
        "e": [0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E],
        "f": [0x06, 0x08, 0x08, 0x1C, 0x08, 0x08, 0x08],
        "g": [0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E],
        "h": [0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11],
        "i": [0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E],
        "j": [0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C],
        "k": [0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12],
        "l": [0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E],
        "m": [0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11],
        "n": [0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11],
        "o": [0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E],
        "p": [0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10],
        "q": [0x00, 0x00, 0x0D, 0x13, 0x0F, 0x01, 0x01],
        "r": [0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10],
        "s": [0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E],
        "t": [0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06],
        "u": [0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D],
        "v": [0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04],
        "w": [0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A],
        "x": [0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11],
        "y": [0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E],
        "z": [0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F],
    }
    raw.update(lower)
    glyphs = []
    for code in range(32, 127):
        ch = chr(code)
        src = raw.get(ch, raw.get(ch.upper(), []))
        rows = []
        # Place the source in the vertical middle, each source row used once
        # then a second copy so the glyph fills about 14px.
        top = 1
        doubled = []
        for bits in src:
            doubled.append(bits)
            doubled.append(bits)
        while len(doubled) < 14:
            doubled.append(0)
        doubled = doubled[:14]
        for y in range(16):
            bits = 0
            if top <= y < top + len(doubled):
                five = doubled[y - top] & 0x1F
                bits = (five << 2) & 0xFF
            rows.append(bits)
        glyphs.append(rows)
    return glyphs


def write_font(glyphs):
    lines = [
        "/* Monospace 8x16, ASCII 32-126. One scan per row, MSB is the left pixel. */",
        '#include "font.h"',
        "",
        "static const uint8_t g_font[95][16] = {",
    ]
    for i, rows in enumerate(glyphs):
        ch = chr(32 + i)
        shown = ch if ch not in ("\\", "'") else " "
        vals = ", ".join("0x%02X" % b for b in rows)
        lines.append("    /* %s */ {%s}," % (shown, vals))
    lines.append("};")
    lines.append("")
    lines.append("uint32_t font_row(unsigned int index, int y) {")
    lines.append("    if (y < 0 || y >= 16 || index < 32u || index > 126u) {")
    lines.append("        return 0;")
    lines.append("    }")
    lines.append("    return g_font[index - 32u][y];")
    lines.append("}")
    lines.append("")
    lines.append("const int font_arial_width = 8;")
    lines.append("const int font_arial_height = 16;")
    lines.append("")
    open(FONT_C, "w", newline="\n").write("\n".join(lines))


def main():
    os.makedirs(OUT, exist_ok=True)
    asm = [
        ".section .rodata",
        ".align 8",
    ]
    specs = []
    for name, filename in ICON_NAMES:
        w, h, pix = read_png(os.path.join(ASSETS, filename))
        rgba = downscale(pix, w, h, 48, 48)
        bin_name = name + ".bin"
        write_bin(os.path.join(OUT, bin_name), rgba)
        sym = "icon_bin_" + name
        asm.append(".global %s" % sym)
        asm.append("%s:" % sym)
        asm.append('.incbin "build/icons/%s"' % bin_name)
        asm.append(".align 8")
        specs.append((sym, 48, 48))
        print(name, w, h, "-> 48x48")
    ww, wh, wpix = read_png(os.path.join(ASSETS, "ChrisOS_Wallpaper.png"))
    dw, dh = fit(ww, wh, 1920, 1080)
    wrgba = downscale(wpix, ww, wh, dw, dh)
    write_bin(os.path.join(OUT, "wall.bin"), wrgba)
    asm.append(".global icon_bin_wall")
    asm.append("icon_bin_wall:")
    asm.append('.incbin "build/icons/wall.bin"')
    open(os.path.join(OUT, "icons.S"), "w", newline="\n").write("\n".join(asm) + "\n")
    print("wallpaper", ww, wh, "->", dw, dh)

    c = ['#include "icons.h"', ""]
    for sym, w, h in specs:
        c.append("extern const uint32_t %s[];" % sym)
    c.append("extern const uint32_t icon_bin_wall[];")
    c.append("")
    c.append("static const RgbaImage g_icons[ICON_N] = {")
    for sym, w, h in specs:
        c.append("    {%s, %d, %d}," % (sym, w, h))
    c.append("};")
    c.append("static const RgbaImage g_wall = {icon_bin_wall, %d, %d};" % (dw, dh))
    c.append("")
    c.append("const RgbaImage *icon_by_id(int id) {")
    c.append("    if (id < 0 || id >= ICON_N) {")
    c.append("        return 0;")
    c.append("    }")
    c.append("    return &g_icons[id];")
    c.append("}")
    c.append("")
    c.append("const RgbaImage *icon_wallpaper(void) {")
    c.append("    return &g_wall;")
    c.append("}")
    c.append("")
    open(ICONS_C, "w", newline="\n").write("\n".join(c))

    glyphs = font_from_pil()
    if glyphs is None:
        print("font: built-in 8x16")
        glyphs = font_fallback()
    else:
        print("font: truetype thresholded to 8x16")
    write_font(glyphs)


if __name__ == "__main__":
    main()
