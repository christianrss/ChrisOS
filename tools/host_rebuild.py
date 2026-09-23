#!/usr/bin/env python3
"""Host side of `rebuild` from the guest (TCP 10.0.2.2:9017)."""
import os
import socket
import subprocess
import sys

PORT = int(os.environ.get("HOST_REBUILD_PORT", "9017"))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", PORT))
    sock.listen(1)
    sys.stderr.write("rebuild listen %d\n" % PORT)
    while True:
        conn, _addr = sock.accept()
        data = b""
        conn.settimeout(5)
        try:
            while b"\n" not in data and len(data) < 64:
                chunk = conn.recv(64)
                if not chunk:
                    break
                data += chunk
        except socket.timeout:
            pass
        if data.startswith(b"rebuild"):
            rc = subprocess.call(["make", "iso"], cwd=ROOT)
            if rc == 0:
                conn.sendall(b"OK\n")
            else:
                conn.sendall(b"ERR\n")
        else:
            conn.sendall(b"ERR\n")
        conn.close()


if __name__ == "__main__":
    main()
