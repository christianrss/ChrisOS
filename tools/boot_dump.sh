#!/bin/bash
set -e
cd "$(dirname "$0")/.."
rm -f /tmp/regs.txt screen_limine.ppm
(
  sleep 5
  printf 'info registers\nscreendump screen_limine.ppm\nquit\n'
) | timeout 14 qemu-system-x86_64 -boot d -cdrom os.iso -m 256M \
    -vga std -display none -monitor stdio > /tmp/regs.txt 2>/dev/null || true
echo "==== key regs ===="
grep -E 'RIP=|CR0=|CR3=|EFER=|CS =' /tmp/regs.txt | head -20
echo "==== ppm ===="
python3 - <<'PY'
from pathlib import Path
p=Path('screen_limine.ppm')
print('exists', p.exists(), 'size', p.stat().st_size if p.exists() else 0)
if p.exists():
    data=p.read_bytes().split(b'\n')
    i=1
    while data[i].startswith(b'#'):
        i+=1
    print('header', data[0], data[i])
PY
