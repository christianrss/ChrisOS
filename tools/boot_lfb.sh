#!/bin/bash
cd /mnt/e/Aulas/ChrisOS
(
  sleep 5
  printf 'info registers\n'
  printf 'xp /8xw 0xfd000000\n'
  printf 'xp /4xw 0xfd001900\n'
  printf 'quit\n'
) | timeout 12 qemu-system-x86_64 -boot d -cdrom os.iso -m 256M -smp 2 \
    -drive file=disk.img,format=raw,if=ide,index=0 \
    -vga std -display none -serial file:/tmp/ser.log -monitor stdio \
    > /tmp/mon.txt 2>/dev/null || true
echo "==== RIP ===="
grep -E 'RIP=|EIP=|HLT=|CPL=' /tmp/mon.txt | head -8
echo "==== LFB ===="
grep -A2 'fd000000' /tmp/mon.txt | head -20
echo "==== serial end ===="
tail -8 /tmp/ser.log
