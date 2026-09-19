#!/usr/bin/env python3
import socket

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.sendto(b"ping", ("127.0.0.1", 7007))
s.settimeout(3)
try:
    data, addr = s.recvfrom(1024)
    print("recv:", repr(data), "from", addr)
except socket.timeout:
    print("recv: timeout")
