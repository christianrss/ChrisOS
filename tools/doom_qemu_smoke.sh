#!/bin/bash
set -e
cd /mnt/e/Aulas/ChrisOS
rm -f /tmp/chrisos_serial.log /tmp/chrisos_qemu.pid
qemu-system-x86_64 -M pc -m 4G -smp 2 -boot order=dc -display none \
  -drive file=build/disk.img,format=raw,if=ide,index=0 \
  -drive file=build/os.iso,format=raw,if=ide,index=2,media=cdrom \
  -serial file:/tmp/chrisos_serial.log -no-reboot -no-shutdown \
  -cpu qemu64 -accel tcg &
echo $! > /tmp/chrisos_qemu.pid
sleep 90
kill "$(cat /tmp/chrisos_qemu.pid)" 2>/dev/null || true
wait 2>/dev/null || true
echo "SERIAL_BYTES=$(wc -c < /tmp/chrisos_serial.log)"
grep -E 'jit|FAULT|doom|Doom|ENGINE|D_Doom|IWAD|falling|ready|run:|native' /tmp/chrisos_serial.log | tail -100 || true
