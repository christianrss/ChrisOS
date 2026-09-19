#!/usr/bin/env python3
"""LEARN:HX23-07 — envia arquivo host → ChrisOS via TCP CFS1 (porta 9016)."""
import os
import socket
import struct
import sys

HOST = os.environ.get("CHRISOS_HOST", "127.0.0.1")
PORT = int(os.environ.get("HOST_XFER_PORT", "9016"))
MAGIC = b"CFS1"
ACK_OK = 0x06
ACK_ERR = 0x15
PATH_MAX = 512
FILE_MAX = 8460288


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} CFS/PATH host-file", file=sys.stderr)
        return 1

    cfs_path = sys.argv[1].upper()
    host_file = sys.argv[2]

    if len(cfs_path) >= PATH_MAX:
        print("CFS path too long", file=sys.stderr)
        return 1

    with open(host_file, "rb") as f:
        data = f.read()

    if len(data) == 0 or len(data) > FILE_MAX:
        print("file size out of range", file=sys.stderr)
        return 1

    path_b = cfs_path.encode("ascii")
    hdr = MAGIC + struct.pack(">II", len(path_b), len(data))
    payload = hdr + path_b + data

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(30.0)
    try:
        sock.connect((HOST, PORT))
        sock.sendall(payload)
        ack = sock.recv(1)
    finally:
        sock.close()

    if len(ack) != 1:
        print("no ack from guest", file=sys.stderr)
        return 2
    if ack[0] == ACK_OK:
        print(f"cfs_send: ok {cfs_path} ({len(data)} bytes)")
        return 0
    print(f"guest error ack=0x{ack[0]:02x}", file=sys.stderr)
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
