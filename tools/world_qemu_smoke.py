#!/usr/bin/env python3
"""Boot with SYS/SMOKE.WORLD=1; verify WORLD.CLV starts without FAULT."""
import os
import signal
import subprocess
import time

ROOT = "/mnt/e/Aulas/ChrisOS"
LOG = "/tmp/chrisos_world_serial.log"
os.chdir(ROOT)

def cfs_put(guest, host_data):
    path = "/tmp/cfs_put_payload"
    open(path, "w").write(host_data)
    subprocess.check_call(
        ["build/host/cfs_put_file", "build/disk.img", guest, path]
    )

cfs_put("SYS/SMOKE.WORLD", "1\n")
cfs_put("SYS/SMOKE.DOOM", "0\n")

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

def read():
    if not os.path.exists(LOG):
        return ""
    return open(LOG, "rb").read().decode("latin1", errors="replace")

deadline = time.time() + 120
text = ""
try:
    while time.time() < deadline:
        text = read()
        if "run: started interp" in text and "WORLD.CLV" in text:
            # give a few seconds of ticks
            time.sleep(8)
            text = read()
            break
        if "FAULT" in text and "WORLD" in text:
            break
        time.sleep(0.5)
finally:
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.kill()
    cfs_put("SYS/SMOKE.WORLD", "0\n")

keys = ("WORLD", "FAULT", "run:", "boot:", "SMOKE", "started", "gfx")
lines = [ln for ln in text.splitlines() if any(k in ln for k in keys)]
print(f"SERIAL_BYTES={len(text.encode('latin1', errors='replace'))}")
for ln in lines[-80:]:
    print(ln)

if "boot: SYS/SMOKE.WORLD" not in text:
    print("FAIL: smoke marker not seen")
    raise SystemExit(2)
if "run: load GAMES/WORLD.CLV" not in text and "WORLD.CLV" not in text:
    print("FAIL: WORLD not loaded")
    raise SystemExit(3)
if "FAULT" in text and "WORLD" in text:
    print("FAIL: WORLD faulted")
    raise SystemExit(4)
if "mesh: drew tris=" not in text:
    print("FAIL: mesh produced no triangles (still blue?)")
    raise SystemExit(5)
if "run: started interp" in text or "run: started jit" in text:
    print("OK: WORLD started and drew mesh")
    raise SystemExit(0)
print("FAIL: world smoke")
raise SystemExit(1)
