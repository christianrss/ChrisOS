#!/bin/bash
set -e
cd "$(dirname "$0")/.."
make 2>&1 | tail -1
rm -f qemu_debug.log
timeout 15 qemu-system-x86_64 -drive format=raw,file=os.img -vga std -display none \
  -d guest_errors -D qemu_debug.log 2>/dev/null || true
echo "guest_errors $(wc -l < qemu_debug.log)"
{ sleep 6; cat tools/mon_cmds.txt; } | timeout 12 qemu-system-x86_64 \
  -drive format=raw,file=os.img -vga std -display none -monitor stdio 2>/dev/null | tail -1
python3 tools/check_mouse.py screen.ppm
python3 tools/find_cursor.py | head -8
