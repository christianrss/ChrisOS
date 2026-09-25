# Chris Kernel C profile

This is the language KCC is allowed to grow into. It is not a second GCC.
A row marked "today: no" is absent at the audit snapshot
`894aed92e48e764e2627ecfc2514684f50809f59`. Shipping a row requires a host
gate, and a kernel file that uses it requires the level gate in
`docs/KERNEL_SELFHOST_PLAN.md`.

The kernel ABI below is the one host GCC already uses. KCC must match it
before it compiles a driver. The kernel does not use the red zone.

## Types

| Type | Today |
| --- | --- |
| `void`, `char`, `signed char`, `unsigned char` | no, except `void` as a function type word |
| `short`, `unsigned short` | no |
| `int`, `unsigned int` | `int` as a function type word only |
| `long`, `unsigned long` | no |
| `long long`, `unsigned long long` | no |
| `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` | function type word only |
| `int8_t`, `int16_t`, `int32_t`, `int64_t` | no |
| `size_t`, `uintptr_t`, `intptr_t` | no |
| `bool` | function type word only |

`char` is a distinct type from `signed char` and `unsigned char`. Integer
promotions and the usual arithmetic conversions apply inside this list.
No `_Float`, no `float`, no `double` in the kernel profile.

Widths on the x86-64 target:

| Type | Width | Alignment |
| --- | --- | --- |
| `char` and the 8-bit integers | 1 | 1 |
| `short` and the 16-bit integers | 2 | 2 |
| `int` and the 32-bit integers | 4 | 4 |
| `long`, `unsigned long`, pointers | 8 | 8 |
| `long long` and the 64-bit integers | 8 | 8 |
| `size_t`, `uintptr_t`, `intptr_t` | 8 | 8 |
| `bool` | 1 | 1 |

These widths are the System V AMD64 LP64 rules. They are a specification
until a layout test compares them with host GCC.

## Pointers, aggregates, qualifiers

Required, and absent today except where the audit says a line is skipped:

- `T *`, `T **`, `void *`, pointer arithmetic, `&`, `*`, `p[i]`
- `struct`, nested struct, pointer to struct, `.`, `->`, arrays of structs
- a basic `union` for register and descriptor views
- `enum`
- `const`, `volatile`, `static`, `extern`

`volatile` reads and writes are observable. The compiler must not delete,
merge, or reorder them across each other. MMIO depends on this.
`host-kcc-test` now checks that for a `volatile uint32_t` global. That is
not a proof for every kernel type or for GNU inline assembly.

## Control flow and expressions

Present in KCC and covered by `host-kcc-test`:

- `if`, `else`, `while`, `for`, `break`, `continue`, `return`
- `+ - * / % & | ^ ~ << >> && || ! == != < <= > >=`
- `= += -= *= /= &= |= ^=`
- casts `(T)x`
- `sizeof(type)` and `sizeof(expression)`, evaluated as a constant when
  the operand is a type. `sizeof(uint32_t)` is 4. `sizeof(int)` is 8.
- `_Static_assert` of an integer constant expression. A non-constant
  expression fails. A zero result fails.

Still absent:

- `do`/`while`, `switch`, `case`, `default`
- `<<=` and `>>=`
- GNU inline asm other than `cli`, `sti`, `hlt`, `pause`, an empty
  barrier, and the port `in`/`out` templates in `port.c`

Division and remainder of signed integers follow the host GCC result for
the same C. A differential test has to lock that down before kernel code
uses them. Shift counts that are negative or greater than or equal to the
width are outside the profile until a test defines them.

## Preprocessor

Required later, and skipped or ignored today:

- `#include` of a quoted or angle header, with a search path from the
  build manifest
- `#define` of object-like constants and function-like macros that expand
  to a token list without token pasting or stringification
- `#ifdef`, `#ifndef`, `#if` on a constant expression of `defined` and
  integer literals, `#else`, `#endif`

`#if` does not evaluate `sizeof` in the first cut. Token pasting, variadic
macros, and `#pragma` are outside the profile until a kernel file needs
them.

## Functions and attributes

Prototypes, multiple translation units, `extern` functions, and `static`
functions are required. Function pointers are required before an indirect
call in a kernel file is compiled. None of that exists. A function header
today only prints a label.

Attributes the kernel needs:

| GCC spelling already in the tree | Profile equivalent | Today |
| --- | --- | --- |
| `__attribute__((packed))` | packed | no |
| `__attribute__((aligned(N)))` | aligned | no |
| `__attribute__((noreturn))` | noreturn | no |

Keep the GCC spelling when the current sources use it. A second spelling
is allowed only for new code and must be documented here before use.
`__asm__` in `kernel/metal/port.c` is not C. It stays an assembly
translation unit until ChrisAsm can emit `in` and `out`. KCC does not
parse GNU inline assembly.

## x86-64 ABI

System V AMD64, with the kernel red zone turned off.

| Role | Register |
| --- | --- |
| arguments 1–6 | RDI, RSI, RDX, RCX, R8, R9 |
| return | RAX, and RDX when a value needs two registers |
| callee-saved | RBX, RBP, R12, R13, R14, R15 |
| argument 7 and later | stack, right to left in the SysV sense, 8-byte slots |

The stack is 16-byte aligned immediately before `call`. `call` pushes an
8-byte return address. A function that calls another function restores
that alignment before the next `call`.

The red zone (128 bytes below RSP) is not available. Every leaf that
needs memory subtracts from RSP. Signal and interrupt frames in the
kernel can clobber a red zone, so generated code must not read or write
below RSP.

Struct returns and arguments follow the SysV classification only for
aggregates the profile can layout. Until the layout gate exists, KCC
must refuse to pass or return a struct in registers rather than guess.

`bool` and integer types narrower than 32 bits are extended as SysV
requires at a call boundary. A gate has to compare that extension with
GCC. It does not exist yet.

## What level 0 is allowed to use

Until the rows above are implemented, the only program KCC may accept is:

- comments
- one or more functions with a profile type word, a name, and `()`
- a body of literal `outb(port, value)` and `return` of an integer literal
  or a bare `return`
- braces

`outb` here is a call lowered into RDI and RSI. It is not evidence that
relocations or the ABI are implemented. Files that need anything else
must fail with file, line, column, severity, and message.
