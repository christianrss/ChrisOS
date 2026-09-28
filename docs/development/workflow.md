# Development workflow

ChrisOS development should keep code, tests and documentation synchronized.

## Start from main

~~~bash
git checkout main
git pull --ff-only
git checkout -b <topic-branch>
~~~

Use a focused branch for one coherent change. Avoid mixing generated artifacts, unrelated refactors and architecture changes.

## Find the owning subsystem

| Change | Primary location |
| --- | --- |
| CPU, memory, interrupts, SMP, processes | <code>kernel/metal/</code> |
| Storage/filesystem/install | <code>kernel/fs/</code> |
| Graphics/input/3D/GPU | <code>kernel/gfx/</code> |
| Desktop/windows | <code>kernel/wm/</code> |
| Networking | <code>kernel/net/</code> |
| ChrisC/CLVM/JIT/toolchain | <code>compiler/</code>, <code>kernel/lang/</code> |
| Guest applications | <code>APPS/</code>, <code>GAMES/</code>, <code>LIB/</code>, <code>SYS/</code> |
| ChrisVM | <code>chrisvm/</code> |

Preserve subsystem boundaries. Do not leak emulator internals into ChrisOS drivers when the virtual-hardware interface can express the behavior.

## Build continuously

Kernel-level work:

~~~bash
make kernel
~~~

Normal system work:

~~~bash
make
~~~

ChrisVM-only work:

~~~bash
make chrisvm
make chrisvm-test
~~~

## Test narrowly, then broadly

Examples:

~~~bash
make host-cfs-test
make host-kcc-test
make test-qemu-gpu
make test-qemu-xhci
make chrisvm-test
~~~

Then expand as appropriate:

~~~bash
make host-gates
make qemu-gates
~~~

Use [testing.md](testing.md) to choose the evidence level appropriate to the claim.

## Update documentation with code

Update a guide when commands or setup change.

Update architecture/specification documentation when a contract changes.

Update current-status documentation when new evidence changes what can be claimed.

Do not rewrite a historical audit merely to make it match newer code. Preserve the audit and update the current-state layer.

The public documentation site is https://os.christiansoftware.org/. Repository-local instructions remain the source of truth for commands coupled to this tree.

## Keep generated output out of Git

Never commit <code>build/</code>, generated ISO/disk images, object files or temporary QEMU logs.

## Pull request quality

A useful pull request states:

- the problem or capability being changed;
- the architectural boundary affected;
- exact test commands run;
- what was not tested;
- compatibility/format impact;
- documentation changed with the implementation.
