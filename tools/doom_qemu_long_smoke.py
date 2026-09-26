#!/usr/bin/env python3
"""Long Doom smoke: boot with SYS/SMOKE.DOOM=1, capture serial for ~120s."""
import os
import signal
import subprocess
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LOG = "/tmp/chrisos_doom_long.log"
os.chdir(ROOT)

def cfs_put(guest, data):
    path = "/tmp/cfs_put_payload"
    open(path, "w").write(data)
    subprocess.check_call(["build/host/cfs_put_file", "build/disk.img", guest, path])

cfs_put("SYS/SMOKE.DOOM", "1\n")
cfs_put("SYS/SMOKE.WORLD", "0\n")
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
    time.sleep(120)
finally:
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.kill()
    cfs_put("SYS/SMOKE.DOOM", "0\n")

text = open(LOG, "rb").read().decode("latin1", errors="replace") if os.path.exists(LOG) else ""
print(f"SERIAL_BYTES={len(text)}")
keys = (
    "jit", "FAULT", "doom", "Doom", "ENGINE", "run:", "IWAD", "falling",
    "started", "alloc", "emit", "native", "SMOKE", "boot:", "ready",
    "pc=", "sp=", "fault=",
)
for ln in text.splitlines():
    if any(k in ln for k in keys):
        print(ln)

fatal = (
    "run: FAULT",
    "run: doom fault",
    "jit: native emit failed",
    "jit: alloc failed",
    "PANIC",
)
ready = (
    "W_OpenFile GAMES/DOOM/DOOM1.WAD" in text
    and ("doom: create done" in text or "run: started jit" in text)
)
if ready and not any(marker in text for marker in fatal):
    print("OK: Doom initialized and remained fault-free")
    raise SystemExit(0)
print("FAIL: Doom did not reach a stable initialized state")
raise SystemExit(1)
