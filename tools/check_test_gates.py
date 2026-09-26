#!/usr/bin/env python3
"""Fail if a host test target is defined but not reachable from host-gates."""
import re
import sys

MAKEFILE = "makefile"
ROOTS = ("host-gates",)


def main():
    text = open(MAKEFILE, encoding="utf-8").read()
    rules = {}
    current = None
    for line in text.splitlines():
        if not line or line[0] in ("\t", "#", " "):
            continue
        match = re.match(r"^([A-Za-z0-9_.-]+)\s*:(.*)$", line)
        if not match:
            current = None
            continue
        name = match.group(1)
        deps = match.group(2).split("#", 1)[0].split()
        rules.setdefault(name, [])
        rules[name].extend(deps)
        current = name
        if line.endswith("\\"):
            continue
        current = None
    # Continuation lines that are still prerequisites are indented; the
    # simple scan above misses those. Re-parse with continuations joined.
    joined = text.replace("\\\n", " ")
    rules = {}
    for line in joined.splitlines():
        if not line or line[0] in ("\t", "#"):
            continue
        match = re.match(r"^([A-Za-z0-9_.-]+)\s*:(.*)$", line)
        if not match:
            continue
        name = match.group(1)
        deps = []
        for tok in match.group(2).split("#", 1)[0].split():
            if tok.startswith("$"):
                continue
            deps.append(tok)
        rules.setdefault(name, [])
        rules[name].extend(deps)

    seen = set()
    stack = list(ROOTS)
    while stack:
        name = stack.pop()
        if name in seen:
            continue
        seen.add(name)
        stack.extend(rules.get(name, []))

    orphans = []
    for name in sorted(rules):
        if name in seen:
            continue
        if name.startswith("test_") or name.startswith("host-") and name.endswith("-test"):
            orphans.append(name)
    if orphans:
        print("orphan tests (not reachable from host-gates):")
        for name in orphans:
            print("  " + name)
        return 1
    print("check_test_gates: ok (%d reachable)" % len(seen))
    return 0


if __name__ == "__main__":
    sys.exit(main())
