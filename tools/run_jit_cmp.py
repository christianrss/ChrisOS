#!/usr/bin/env python3
import subprocess
import sys

pcs = [427277, 430940, 463940, 537272, 610608, 720608, 793940, 819572, 820901]
for pc in pcs:
    print(f"=== {pc} ===", flush=True)
    r = subprocess.run(
        ["./build/host/test_doom_jit_cmp", str(pc)],
        cwd="/mnt/e/Aulas/ChrisOS",
        capture_output=True,
        text=True,
    )
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stdout.write(r.stderr)
        print("exit", r.returncode)
