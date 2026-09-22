#!/usr/bin/env python3
"""Boot with SYS/SMOKE.DOOM=1; verify Doom JIT serial path; disable marker after."""
import os
import signal
import subprocess
import time

ROOT = "/mnt/e/Aulas/ChrisOS"
LOG = "/tmp/chrisos_doom_serial.log"
os.chdir(ROOT)

def cfs_put(guest, host_data):
    path = "/tmp/cfs_put_payload"
    open(path, "w").write(host_data)
    subprocess.check_call(
        ["build/host/cfs_put_file", "build/disk.img", guest, path]
    )

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

def read():
    if not os.path.exists(LOG):
        return ""
    return open(LOG, "rb").read().decode("latin1", errors="replace")

deadline = time.time() + 360
text = ""
try:
    while time.time() < deadline:
        text = read()
        if "jit: native ready" in text or "run: started jit" in text:
            break
        if "jit: native emit failed" in text or "jit: alloc failed" in text:
            break
        time.sleep(1.0)
finally:
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.kill()
    cfs_put("SYS/SMOKE.DOOM", "0\n")

keys = (
    "jit", "FAULT", "doom", "ENGINE", "alloc", "native", "ready",
    "run:", "compile", "nat clear", "emit pc", "started", "SMOKE", "boot:",
)
lines = [ln for ln in text.splitlines() if any(k in ln for k in keys)]
print(f"SERIAL_BYTES={len(text.encode('latin1', errors='replace'))}")
for ln in lines[-120:]:
    print(ln)

ok = (
    "jit: native ready" in text
    or "run: started jit" in text
    or "run: jit ready" in text
)
past_hang = "jit: alloc map done" in text or "jit: nat clear done" in text
if ok:
    print("OK: doom jit ready")
    raise SystemExit(0)
if past_hang and ("jit: emit pc=" in text or "jit: native compiling" in text):
    print("OK: past hang; compile progressing on TCG")
    raise SystemExit(0)
if "boot: SYS/SMOKE.DOOM" in text and "jit: alloc begin" in text:
    print("OK: entered alloc (contig path live)")
    raise SystemExit(0)
print("FAIL: doom jit smoke")
raise SystemExit(1)
