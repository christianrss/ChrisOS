#!/bin/sh
set -e
cd "$(dirname "$0")/.."
gcc -std=c11 -Wall -Wextra -Werror -Icompiler/chrisc -Icompiler/clvm \
    -Icompiler/cls -Icompiler/gc -Icompiler \
    -o build/host/cc_one tools/cc_one.c compiler/chrisc/chrisc.c \
    compiler/clvm/clasm.c compiler/clvm/clvm_format.c \
    compiler/cls/cls.c compiler/gc/gc.c
gcc -std=c11 -Wall -Wextra -Werror -Icompiler/clvm -Icompiler \
    -o build/host/run_cc tools/run_cc.c compiler/clvm/clvm_vm.c \
    compiler/clvm/clvm_format.c
./build/host/cc_one APPS/CC/CC.CC build/CC1.CLV
./build/host/run_cc build/CC1.CLV "APPS/CC/CC.CC build/CC2.CLV"
./build/host/run_cc build/CC2.CLV "APPS/CC/CC.CC build/CC3.CLV"
cmp build/CC2.CLV build/CC3.CLV
./build/host/run_cc build/CC2.CLV "APPS/CC/AB.CC build/AB.CLV"
./build/host/run_cc build/AB.CLV | grep "stack 7"
echo cc-fixed-ok
