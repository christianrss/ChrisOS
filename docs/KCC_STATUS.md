# KCC status

Audit snapshot: `894aed92e48e764e2627ecfc2514684f50809f59`.
Profile: `docs/CHRIS_KERNEL_C_PROFILE.md`.
Full gap: `docs/NATIVE_TOOLCHAIN_AUDIT.md`.

## Implemented

`kcc_compile_named` walks one line at a time and records a diagnostic
(`file`, `line`, `column`, `severity`, `message`). Severity 1 is an error.

Level 0 accepts comments, a function whose type word is `void`, `bool`,
`int`, or `uint8_t` / `uint16_t` / `uint32_t` / `uint64_t`, a body of
literal `outb` and `return` of an integer literal or a bare `return`, and
braces. `outb` is lowered to a call with the port in RDI and the value in
RSI. ChrisAsm records that call as an undefined symbol and an
`R_X86_64_PLT32` relocation. ChrisLd applies it when another object
defines the symbol.

A preprocessor line, `static`, a declaration, and any other statement
fail the compile. The assembly buffer no longer truncates in silence.

## Not implemented

Lexer, parser, AST, semantic analysis, IR, and an x86-64 code generator
as separate units. The rest of the kernel C profile. Volatile. Struct
layout. A comparison against host GCC. Real relocations.

## Gate

`host-kcc-kernel-l0` runs `host-kcc-test`. The test compiles
`tools/kcc_fixtures/level0.c` and rejects `kernel/metal/serial.c` on its
preprocessor line. `serial.c` is level 1 and is not compiled.
`host-kcc-test` remains a dependency of `host-gates`.

Passing this gate does not mark SH4.
