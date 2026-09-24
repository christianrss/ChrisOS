#!/usr/bin/env python3
"""Run a QEMU boot test and judge it by markers, not by a ignored timeout.

A kernel that stays up until the timeout is a pass when every --expect
string is in the serial log and no fatal marker is present. A QEMU crash,
a panic, or a missing marker is a failure.
"""
import argparse
import subprocess
import sys

FATALS = (
    "PANIC:",
    "EXCEPTION vector=",
    "double fault",
    "general protection",
    "heap corruption",
    "PMM corruption",
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=int, required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--expect", action="append", default=[])
    parser.add_argument("cmd", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    cmd = args.cmd
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        print("qemu_gate: missing command", file=sys.stderr)
        return 2
    try:
        proc = subprocess.run(cmd, timeout=args.timeout)
        code = proc.returncode
    except subprocess.TimeoutExpired:
        code = 124
    try:
        with open(args.log, "rb") as fh:
            log = fh.read().decode("utf-8", "replace")
    except OSError as exc:
        print("qemu_gate: cannot read log: %s" % exc, file=sys.stderr)
        return 1
    failed = False
    if code not in (0, 124):
        print("qemu_gate: qemu exit %s" % code, file=sys.stderr)
        failed = True
    for fatal in FATALS:
        if fatal in log:
            print("qemu_gate: fatal marker %r" % fatal, file=sys.stderr)
            failed = True
    for expect in args.expect:
        if expect not in log:
            print("qemu_gate: missing marker %r" % expect, file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
