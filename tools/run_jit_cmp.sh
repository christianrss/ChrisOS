#!/bin/bash
cd /mnt/e/Aulas/ChrisOS
for pc in 427277 430940 463940 537272 610608 720608 793940 819572 820901; do
  echo "=== $pc ==="
  ./build/host/test_doom_jit_cmp "$pc" 2>/dev/null
done
