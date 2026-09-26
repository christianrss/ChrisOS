#!/usr/bin/env bash
#
# Cloud Agent bootstrap for ChrisOS.
#
# ChrisOS is a hobby x86-64 operating system that boots under QEMU via the
# Limine bootloader. This script prepares a fresh checkout so the OS can be
# built (`make iso`, `make disk.img`) and run (`make run`).
#
# It is idempotent: it can be run repeatedly and against a partially prepared
# tree without failing.
#
# Two vendored dependencies live at submodule paths that have no .gitmodules
# entry, so they cannot be restored with `git submodule update`. They are
# fetched here at the exact commits the repository pins:
#   third_party/limine          -> limine-bootloader/limine  @ v9.x-binary
#   third_party/doomgeneric_src  -> ozkl/doomgeneric          @ dcb7a8d
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

LIMINE_SHA="ee5d29cd0a8034612dcd1df3f00052480db785c5"   # v9.x-binary tip; API revision 3
LIMINE_URL="https://github.com/limine-bootloader/limine.git"
DOOM_SHA="dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284"
DOOM_URL="https://github.com/ozkl/doomgeneric.git"

log() { printf '\n=== %s ===\n' "$*"; }

# ---------------------------------------------------------------------------
# 1. System toolchain
# ---------------------------------------------------------------------------
# ChrisOS needs: gcc (freestanding -m64), nasm (interrupt stubs), GNU ld,
# xorriso (hybrid ISO), qemu-system-x86_64, python3. GCC 12+ emits a
# false-positive -Werror=array-bounds in compiler/chrisc/chrisc.c at -O2, so
# the project builds cleanly with gcc-11; we install it and make it the
# default `gcc` (the makefile invokes `gcc`/`HOST_CC=gcc` unqualified).
log "Installing system packages"
export DEBIAN_FRONTEND=noninteractive
sudo apt-get update -qq
sudo apt-get install -y -qq \
    build-essential nasm xorriso mtools qemu-system-x86 gcc-11 python3 git curl

if command -v gcc-11 >/dev/null 2>&1; then
    log "Selecting gcc-11 as the default compiler"
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 110 >/dev/null 2>&1 || true
    if command -v gcc-13 >/dev/null 2>&1; then
        sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 60 >/dev/null 2>&1 || true
    fi
    sudo update-alternatives --set gcc /usr/bin/gcc-11 >/dev/null 2>&1 || true
fi
gcc --version | head -1

# ---------------------------------------------------------------------------
# 2. Vendored dependency: Limine bootloader (prebuilt binary branch)
# ---------------------------------------------------------------------------
# Provides limine.h (kernel include), the BIOS/UEFI boot blobs copied into the
# ISO staging tree, and the `limine` host tool (built from limine.c) used for
# `limine bios-install`.
if [ ! -f third_party/limine/limine.h ]; then
    log "Fetching Limine @ ${LIMINE_SHA} (v9.x-binary)"
    rm -rf third_party/limine
    git clone --depth 1 --branch v9.x-binary "$LIMINE_URL" third_party/limine
    git -C third_party/limine fetch --depth 1 origin "$LIMINE_SHA" >/dev/null 2>&1 || true
    git -C third_party/limine checkout -q "$LIMINE_SHA" 2>/dev/null || true
else
    log "Limine already present"
fi
if [ ! -x third_party/limine/limine ]; then
    log "Building the limine host tool"
    make -C third_party/limine
fi

# ---------------------------------------------------------------------------
# 3. Vendored dependency: doomgeneric (DOOM engine sources)
# ---------------------------------------------------------------------------
# GAMES/DOOM/ENGINE.LST references third_party/doomgeneric_src/doomgeneric/*.c
# for the host DOOM engine gate (test_doom_engine) and the guest DOOM build.
if [ ! -d third_party/doomgeneric_src/doomgeneric ]; then
    log "Fetching doomgeneric @ ${DOOM_SHA}"
    rm -rf third_party/doomgeneric_src
    git clone --depth 1 "$DOOM_URL" third_party/doomgeneric_src
    git -C third_party/doomgeneric_src fetch --depth 1 origin "$DOOM_SHA" >/dev/null 2>&1 || true
    git -C third_party/doomgeneric_src checkout -q "$DOOM_SHA" 2>/dev/null || true
else
    log "doomgeneric already present"
fi

# ---------------------------------------------------------------------------
# 4. Build verification
# ---------------------------------------------------------------------------
# Build the bootable ISO (kernel + Limine + xorriso) and the CFS data disk so a
# fresh agent has ready-to-run artifacts. `make run` regenerates them as needed.
log "Building ChrisOS ISO"
make iso

log "Building CFS data disk (build/disk.img)"
make disk.img

log "ChrisOS environment ready"
echo "Build: make iso | Disk: make disk.img | Run: make run"
echo "Note: nested KVM is unavailable in Cloud Agent VMs; run QEMU with '-accel tcg'"
echo "      (e.g. 'make run QEMU_ACCEL=tcg' if supported, or override the accel flag)."
