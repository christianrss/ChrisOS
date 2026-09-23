#!/usr/bin/env python3
"""Check a ChrisOS install image: primary GPT, backup GPT, ESP name, ChrisFS."""
import struct
import sys
import zlib

CHRIS_GUID = bytes([
    0x53, 0x46, 0x52, 0x43, 0x00, 0x31, 0x00, 0x40,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
])
CFS_MAGIC = 0x31534643


def crc_hdr(hdr):
    buf = bytearray(hdr[:92])
    buf[16:20] = b"\0\0\0\0"
    return zlib.crc32(buf) & 0xFFFFFFFF


def main():
    path = sys.argv[1]
    data = open(path, "rb").read()
    if len(data) < 512 * 67:
        sys.exit("install image too small")
    if data[510:512] != b"\x55\xaa" or data[450] != 0xEE:
        sys.exit("protective MBR missing")
    hdr = data[512:512 + 92]
    if hdr[:8] != b"EFI PART":
        sys.exit("primary GPT signature missing")
    stored = struct.unpack_from("<I", hdr, 16)[0]
    got = crc_hdr(hdr)
    if got != stored:
        sys.exit("primary GPT crc %08x != %08x" % (got, stored))
    bak = data[-512:]
    if bak[:8] != b"EFI PART":
        sys.exit("backup GPT signature missing")
    bstored = struct.unpack_from("<I", bak, 16)[0]
    if crc_hdr(bak) != bstored:
        sys.exit("backup GPT crc mismatch")
    entries = data[1024:1024 + 256]
    if entries[128:144] != CHRIS_GUID:
        sys.exit("ChrisFS partition GUID missing")
    start = struct.unpack_from("<Q", entries, 128 + 32)[0]
    super_off = start * 512
    magic = struct.unpack_from("<I", data, super_off)[0]
    if magic != CFS_MAGIC:
        sys.exit("ChrisFS superblock missing at LBA %d" % start)
    if b"BOOTX64 EFI" not in data:
        sys.exit("ESP directory entry BOOTX64.EFI missing")
    if b"KERNEL  ELF" not in data:
        sys.exit("ESP kernel.elf entry missing")
    if b"limine.conf" not in data:
        sys.exit("ESP limine.conf LFN missing")
    print("install image ok sectors=%d cfs_lba=%d" % (len(data) // 512, start))


if __name__ == "__main__":
    main()
