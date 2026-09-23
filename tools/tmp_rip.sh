#!/bin/sh
nm -n build/iso/boot/kernel.elf | awk '$1 ~ /^[0-9a-f]+$/ {
  addr = strtonum("0x" $1)
  if (addr <= 0xffffffff80061a70) last = $0
}
END { print last }'
echo '---'
objdump -d --start-address=0xffffffff80061a40 --stop-address=0xffffffff80061ab0 build/iso/boot/kernel.elf
