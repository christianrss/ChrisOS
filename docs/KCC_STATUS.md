# KCC status

Audit snapshot: `894aed92e48e764e2627ecfc2514684f50809f59`.
Profile: `docs/CHRIS_KERNEL_C_PROFILE.md`.
Full gap: `docs/NATIVE_TOOLCHAIN_AUDIT.md`.

## Implemented

One translation function, `kcc_compile_source`, in `compiler/kcc/kcc.c`.
It prints ChrisAsm for a function label, a literal `outb` call, and
`return` of an integer. The `outb` lowering uses RDI and RSI and then
`call outb`. That call is not relocated.

## Not implemented

Lexer, parser, AST, semantic analysis, IR, and an x86-64 code generator
as separate units. Diagnostics with file, line, column, severity, and
message. The types, statements, and preprocessor in the kernel C profile.
Volatile. Struct layout. A comparison against host GCC.

Unknown statements are ignored and the compile still returns success.

## Gate

`host-kcc-test` runs `tools/test_kcc.c`. The test reads
`kernel/metal/serial.c` and accepts a non-empty text section. `serial.c`
is not in the implemented subset. The pass does not mean level 1.

No `host-kcc-kernel-l0` target exists at this snapshot. Level 0 is not
proven until a gate accepts only the level-0 shape and rejects
`kernel/metal/serial.c`.
