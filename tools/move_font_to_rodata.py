#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
source_path = root / "boot" / "font.c"
target_path = root / "kernel" / "font.c"
source = source_path.read_text(encoding="utf-8")

if "int getArialCharacter(int index, int y)" not in source:
    sys.exit("boot/font.c: getArialCharacter nao encontrado")

source = re.sub(
    r"int\s+getArialCharacter\s*\(\s*int\s+index\s*,\s*int\s+y\s*\)\s*\{",
    "",
    source,
    count=1,
)

arrays = re.findall(r"unsigned int\s+(characters_arial_[0-7])\s*\[\]\[150\]", source)
if arrays != [f"characters_arial_{i}" for i in range(8)]:
    sys.exit("boot/font.c: esperados characters_arial_0..7, obtidos " + str(arrays))

source = re.sub(
    r"unsigned int\s+(characters_arial_[0-7])\s*\[\]\[150\]",
    r"static const uint32_t \1[][150]",
    source,
)

marker = "\tint start = (int)(' ');"
if marker not in source:
    sys.exit("boot/font.c: seletor int start nao encontrado")

prologue = """uint32_t font_row(unsigned int index, int y) {
    unsigned int start = (unsigned int)(' ');
    if (y < 0 || y >= 15 || index < start ||
        index >= start + 13u * 8u) {
        return 0;
    }
"""
source = source.replace(marker, prologue, 1)
source = re.sub(
    r"(return characters_arial_7\[index - \(start \+ 13 \* 7\)\]\[y\];\s*\}\s*)\}",
    r"\1    return 0;\n}",
    source,
    count=1,
)

header = '#include "font.h"\n#include <stdint.h>\n\n'
target_path.parent.mkdir(parents=True, exist_ok=True)
text = header + source.strip() + "\n"
if text.count("static const uint32_t characters_arial_") != 8:
    sys.exit("kernel/font.c gerado sem os oito arrays")
if "uint32_t font_row(" not in text:
    sys.exit("kernel/font.c gerado sem font_row")
if "const int font_arial_width = 10;" not in text:
    sys.exit("kernel/font.c perdeu font_arial_width")
target_path.write_text(text, encoding="utf-8")
print("kernel/font.c gerado com a bitmap original em .rodata")
