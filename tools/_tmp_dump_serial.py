#!/usr/bin/env python3
t = open("/tmp/chrisos_doom_csp.log", "rb").read().decode("latin1", "replace")
i = t.find("started jit")
print("idx", i, "len", len(t))
if i >= 0:
    print(repr(t[i : i + 1200]))
