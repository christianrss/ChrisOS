# ChrisC self-host conformance

`compiler/chrisc` is the bootstrap compiler, compiled by the host and linked
into the kernel. `APPS/CC/CC.CC` is the only compiler written in ChrisC.
They are not equivalent. A cell is PASS only when this tree shows the
feature. Nothing here was marked PASS by aspiration.

Checked by reading `APPS/CC/CC.CC` and the bootstrap compiler sources on
this branch. `APPS/CC/CC.CC` contains no `float`, `typedef`, `#include`, or
`union` token.

| Feature | Bootstrap `compiler/chrisc` | `APPS/CC` |
| --- | --- | --- |
| int | PASS | PASS |
| functions | PASS | PASS |
| struct | PASS | PASS |
| float | PASS | FAIL |
| float* | PASS | FAIL |
| #include | PASS | FAIL |
| #define | PASS | FAIL |
| typedef | PASS | FAIL |
| union | PASS | FAIL |
| casts | PASS | FAIL |
| multi-file | PASS | FAIL |
| diagnostics with file, line, column | PASS | FAIL |
| source maps / `.CDBG` | PASS | FAIL |
| Doom-sized ChrisC input | PASS on the bootstrap compiler | FAIL |

There is no shared conformance suite that runs the same programs through
both compilers yet. Until that suite exists, this table is a source audit,
not an execution gate, and it does not prove SH2.

## Bootstrap stages

Not implemented.

```
STAGE0  compiler/chrisc built by the host
STAGE1  a ChrisC compiler, produced by STAGE0, running inside ChrisOS
STAGE2  STAGE1 compiling its own source again
```

STAGE1 and STAGE2 have to match on bytecode, or on a documented semantic
comparison when metadata is allowed to differ. Byte identity is the
preferred check. No such comparison runs today.
