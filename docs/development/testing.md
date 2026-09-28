# Testing and evidence

ChrisOS has several classes of tests. They answer different questions and should not be treated as interchangeable.

## Evidence levels

| Level | What it proves |
| --- | --- |
| Source present | Code exists; no execution claim |
| Host-tested | Logic passed a host-side test |
| QEMU-tested | The guest path executed under the specified QEMU configuration |
| Hardware-tested | The path executed on identified physical hardware |

A host unit test does not prove a device driver on hardware. A QEMU device test does not prove compatibility with arbitrary physical devices.

## Continuous integration

The current GitHub Actions workflow is <code>.github/workflows/host-foundation.yml</code>. It exercises a focused host-side foundation set on pushes and pull requests.

Do not assume every QEMU or graphics gate runs in hosted CI.

## Host gates

~~~bash
make host-gates
make host-sanitize
make host-stress
~~~

## QEMU gates

~~~bash
make qemu-gates
~~~

Individual targets include:

- <code>test-qemu-ata</code>
- <code>test-qemu-ahci</code>
- <code>test-qemu-nvme</code>
- <code>test-qemu-vblk</code>
- <code>test-qemu-usb</code>
- <code>test-qemu-gpu</code>
- <code>test-qemu-riscv</code>
- <code>test-qemu-noata</code>
- <code>test-qemu-install</code>
- <code>test-qemu-safe</code>
- <code>test-qemu-xhci</code>

These headless gates use TCG unless a target explicitly says otherwise.

## VirGL

~~~bash
make test-qemu-virgl
~~~

This gate requires host VirGL/OpenGL support. A host capability skip means "not tested on this host"; it is neither a guest pass nor a guest failure.

## ChrisVM

~~~bash
make chrisvm-test
~~~

The ChrisVM suite validates emulator/machine behavior and guest fixtures independently from the full QEMU boot path.

## Broad local gate

~~~bash
make full-gates
~~~

## Record evidence

For a capability-changing pull request, record:

~~~text
Commit:
Host:
Command:
Result:
Relevant serial/test marker:
Not tested:
~~~

For hardware evidence, also record machine/device identity and firmware mode.

## Failure triage

1. Keep the first failure and serial log.
2. Rerun the narrow target, not the entire suite.
3. Distinguish host dependency failures from guest failures.
4. Expand to broader gates only after the narrow path is stable.

QEMU gate logs belong under <code>build/</code> and should not be committed.
