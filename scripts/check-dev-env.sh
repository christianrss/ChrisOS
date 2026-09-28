#!/usr/bin/env sh
set -eu

required="git make gcc ld nasm xorriso qemu-system-x86_64 python3"
optional="qemu-system-riscv64 clang ld.lld sdl2-config"
missing=0

echo "ChrisOS development environment"
echo "==============================="
echo
echo "Required tools:"
for cmd in $required; do
  if command -v "$cmd" >/dev/null 2>&1; then
    printf "  [ok]      %s\n" "$cmd"
  else
    printf "  [missing] %s\n" "$cmd"
    missing=1
  fi
done

echo
echo "Optional capabilities:"
for cmd in $optional; do
  if command -v "$cmd" >/dev/null 2>&1; then
    printf "  [ok]       %s\n" "$cmd"
  else
    printf "  [optional] %s\n" "$cmd"
  fi
done

echo
if [ -e /dev/kvm ]; then
  if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    echo "  [ok]      /dev/kvm is readable/writable (interactive make run can use KVM)"
  else
    echo "  [warning] /dev/kvm exists but is not accessible to this user"
  fi
else
  echo "  [optional] /dev/kvm not found (TCG-based QEMU gates still work)"
fi

if [ "$missing" -ne 0 ]; then
  echo
  echo "One or more required tools are missing."
  exit 1
fi

echo
echo "Core ChrisOS build prerequisites are available."
