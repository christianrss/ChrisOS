#!/usr/bin/env python3
"""QEMU Doom JIT smoke: boot ENGINE, capture serial for ~90s."""
import os
import signal
import subprocess
import time

ROOT = "/mnt/e/Aulas/ChrisOS"
LOG = "/tmp/chrisos_serial.log"
os.chdir(ROOT)
if os.path.exists(LOG):
    os.remove(LOG)

cmd = [
    "qemu-system-x86_64", "-M", "pc", "-m", "4G", "-smp", "2",
    "-boot", "order=dc", "-display", "none",
    "-drive", "file=build/disk.img,format=raw,if=ide,index=0",
    "-drive", "file=build/os.iso,format=raw,if=ide,index=2,media=cdrom",
    "-serial", f"file:{LOG}",
    "-no-reboot", "-no-shutdown",
    "-cpu", "qemu64", "-accel", "tcg",
]
proc = subprocess.Popen(cmd)
try:
    time.sleep(60)
finally:
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

data = open(LOG, "rb").read() if os.path.exists(LOG) else b""
text = data.decode("latin1", errors="replace")
print(f"SERIAL_BYTES={len(data)}")
keys = ("jit", "FAULT", "doom", "Doom", "ENGINE", "D_Doom", "IWAD",
        "falling", "ready", "run:", "native", "alloc")
lines = [ln for ln in text.splitlines() if any(k in ln for k in keys)]
for ln in lines[-100:]:
    print(ln)
