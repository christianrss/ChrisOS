#!/usr/bin/env python3
"""Embed ChrisOS GLSL sources as C string literals."""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PAIRS = [
    ("SH_SRC_TRI_VERT", "kernel/gfx/shader/glsl/tri.vert"),
    ("SH_SRC_TRI_FRAG", "kernel/gfx/shader/glsl/tri.frag"),
    ("SH_SRC_VARY_VERT", "kernel/gfx/shader/glsl/vary.vert"),
    ("SH_SRC_VARY_FRAG", "kernel/gfx/shader/glsl/vary.frag"),
    ("SH_SRC_MVP_VERT", "kernel/gfx/shader/glsl/mvp.vert"),
    ("SH_SRC_TEX_VERT", "kernel/gfx/shader/glsl/tex.vert"),
    ("SH_SRC_TEX_FRAG", "kernel/gfx/shader/glsl/tex.frag"),
    ("SH_SRC_LIGHT_VERT", "kernel/gfx/shader/glsl/light.vert"),
    ("SH_SRC_LIGHT_FRAG", "kernel/gfx/shader/glsl/light.frag"),
    ("SH_SRC_WORLD_VERT", "kernel/gfx/shader/glsl/world.vert"),
    ("SH_SRC_WORLD_FRAG", "kernel/gfx/shader/glsl/world.frag"),
]


def c_string(text: str) -> str:
    out = ['"']
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            continue
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def main() -> int:
    dest = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "kernel/gfx/shader/sh_src.h"
    lines = [
        "#ifndef CHRIS_SH_SRC_H",
        "#define CHRIS_SH_SRC_H",
        "/* Generated from kernel/gfx/shader/glsl. ChrisOS GLSL subset, not GLSL 3.30. */",
    ]
    for name, rel in PAIRS:
        text = (ROOT / rel).read_text()
        lines.append("static const char " + name + "[] = " + c_string(text) + ";")
    lines.append("#endif")
    dest.write_text("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
