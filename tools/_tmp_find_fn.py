#!/usr/bin/env python3
"""Find which source function maps to PC 14539 via ENGINE map if any."""
import os
# Try .MAP or similar
for root, dirs, files in os.walk("GAMES/DOOM"):
    for f in files:
        if f.endswith((".MAP", ".map", ".LST", ".lst")):
            print(os.path.join(root, f))
# Also search clv for string near function - skip
# Check ChrisC line map generation
print("code_size check done")
