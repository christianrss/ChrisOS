#!/usr/bin/env python3
"""Quick Doom smoke until FAULT with call-stack dump."""
import os
import signal
import subprocess
import time

ROOT = "/mnt/e/Aulas/ChrisOS"
LOG = "/tmp/chrisos_doom_csp.log"
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
    "qemu-system-x86_64", "-M", "pc", "-m", "4G", "-smp", "1",
    "-boot", "order=dc", "-display", "none",
    "-drive", "file=build/disk.img,format=raw,if=ide,index=0",
    "-drive", "file=build/os.iso,format=raw,if=ide,index=2,media=cdrom",
    "-serial", f"file:{LOG}",
    "-no-reboot", "-no-shutdown",
    "-cpu", "qemu64", "-accel", "tcg",
]
proc = subprocess.Popen(cmd)
try:
    saw_jit = False
    for _ in range(300):
        time.sleep(1)
        if not os.path.exists(LOG):
            continue
        text = open(LOG, "rb").read().decode("latin1", errors="replace")
        if "started jit" in text and not saw_jit:
            saw_jit = True
        if "repair ctor list cursor" in text:
            time.sleep(90)
            break
        if any(k in text for k in (
            "IWAD", "Z_Init", "W_Init", "M_Init", "title", "HU_Init",
            "doomgeneric", "I_Init",
        )):
            time.sleep(20)
            break
        if "FAULT" in text and saw_jit:
            time.sleep(2)
            break
        if saw_jit and _ > 240:
            break
finally:
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.kill()
    cfs_put("SYS/SMOKE.DOOM", "0\n")

text = open(LOG, "rb").read().decode("latin1", errors="replace") if os.path.exists(LOG) else ""
print(f"SERIAL_BYTES={len(text)}")
for ln in text.splitlines():
    if any(k in ln for k in (
        "FAULT", "falling", "native ready", "started jit", "SMOKE", "calls=",
        "boot:", "repair", "IWAD", "doom", "Doom", "vg_", "frame", "title"
    )):
        print(ln)
