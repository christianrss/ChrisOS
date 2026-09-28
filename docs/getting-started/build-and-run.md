# Build and run

All commands are executed from the repository root.

## Build x86-64 ChrisOS

~~~bash
make
~~~

The default target produces <code>build/os.iso</code>.

Kernel only:

~~~bash
make kernel
~~~

Persistent ChrisFS workspace disk:

~~~bash
make disk.img
~~~

## Run interactively

~~~bash
make run
~~~

The current recipe boots the ISO plus <code>build/disk.img</code>, attaches several emulated storage devices, VirtIO-GPU, VirtIO-net, AC97 and USB devices, and exposes host-forwarded ports.

The recipe currently uses <code>-accel kvm</code>. If KVM is unavailable, use the TCG-based integration gates for validation or intentionally adjust your local QEMU command.

## Headless smoke test

~~~bash
make test-qemu-ata
~~~

This gate uses TCG and checks serial markers for CPU bring-up, root storage and ChrisFS mount.

## Integration sets

~~~bash
make host-gates
make qemu-gates
make full-gates
~~~

The QEMU gates are defined in <code>scripts/qemu.mk</code>. See [Testing and evidence](../development/testing.md) for their meaning.

## ChrisVM

~~~bash
make chrisvm
make chrisvm-test
~~~

ChrisVM is built and tested from this repository. Its maintained architecture and roadmap documentation lives at **https://os.christiansoftware.org/**.

## RISC-V bring-up

~~~bash
make riscv
make run-riscv
~~~

This is a bring-up path, not feature parity with x86-64.

## Seed the guest toolchain

~~~bash
make seed-selfhost
~~~

## Copy files into ChrisFS

With QEMU stopped:

~~~bash
make disk-put CFS_PATH=SYS/FILE.C HOST_FILE=./FILE.C
~~~

With the guest transfer service running:

~~~bash
make send CFS_PATH=SYS/LIVE.C HOST_FILE=./LIVE.C
~~~

The default transfer port is TCP 9016.

## Installer integration gate

~~~bash
make test-qemu-install
~~~

Installer architecture, disk layout, limitations, and hardware implications are documented on **https://os.christiansoftware.org/** rather than duplicated here.

## Clean

~~~bash
make clean
~~~

Everything under <code>build/</code> should be reproducible from source.
