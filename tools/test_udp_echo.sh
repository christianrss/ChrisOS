#!/bin/sh
# UDP echo gate for ChrisOS (guest port 7, host port 7007 via make run).
# ncat -u often fails with QEMU user netdev; nc -u and python work reliably.
set -e
HOST=127.0.0.1
PORT="${HOST_NET_PORT:-7007}"
MSG="${1:-ping}"

if command -v python3 >/dev/null 2>&1; then
	exec python3 "$(dirname "$0")/test_udp_echo.py"
fi

if ! command -v nc >/dev/null 2>&1; then
	echo "need python3 or nc (ncat -u is unreliable here)" >&2
	exit 1
fi

printf '%s' "$MSG" | nc -u -w 2 "$HOST" "$PORT"
