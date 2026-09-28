# ChrisOS

<p align="center"><strong>An experimental operating-system and systems-research platform built from the kernel upward.</strong></p>

<p align="center">
  <a href="https://os.christiansoftware.org/">Documentation</a> ·
  <a href="https://os.christiansoftware.org/en/">English</a> ·
  <a href="https://os.christiansoftware.org/pt-br/">Português</a> ·
  <a href="docs/README.md">Repository docs</a> ·
  <a href="CONTRIBUTING.md">Contributing</a>
</p>

<p align="center">
  <a href="https://github.com/christianrss/ChrisOS/actions/workflows/host-foundation.yml"><img alt="host-foundation" src="https://github.com/christianrss/ChrisOS/actions/workflows/host-foundation.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
  <a href="https://os.christiansoftware.org/"><img alt="Docs" src="https://img.shields.io/badge/docs-os.christiansoftware.org-555.svg"></a>
</p>

[Português (Brasil)](README.pt-BR.md)

ChrisOS is a research and educational operating-system project centered on an x86-64 higher-half kernel, a native desktop, storage and network drivers, ChrisFS, the ChrisC/CLVM language stack, a native toolchain, graphics experiments, and the emerging ChrisVM/ChrisCPU virtualization stack.

The repository contains the implementation. The **canonical ChrisOS documentation** lives at **https://os.christiansoftware.org/**. This repository intentionally keeps only operational documentation for environment setup, build/run commands, testing, and contribution workflows.

> **Project status:** ChrisOS is experimental. QEMU is the primary integration environment. Real-hardware support is not yet a general supported deployment target.

## Quick start

The reference development environment is a recent Debian/Ubuntu Linux host.

~~~bash
git clone https://github.com/christianrss/ChrisOS.git
cd ChrisOS

./scripts/check-dev-env.sh
make
make disk.img
make run
~~~

<code>make run</code> currently starts the interactive x86-64 environment with KVM acceleration. For a headless TCG-based smoke test that does not require KVM:

~~~bash
make test-qemu-ata
~~~

See [Development environment](docs/getting-started/environment.md) and [Build and run](docs/getting-started/build-and-run.md) before troubleshooting host-specific issues.

## What is in ChrisOS?

| Area | Implementation |
| --- | --- |
| Kernel | x86-64 higher-half kernel; GDT/TSS, IDT, paging, PMM/heap, SMP, processes, timers and interrupts |
| Boot | Limine BIOS/UEFI boot path |
| Storage | ATA, AHCI, NVMe, VirtIO block, USB mass-storage paths and ChrisFS |
| Graphics | Limine framebuffer, 2D compositor, software 3D, VirtIO-GPU and VirGL experiments |
| Desktop | Window manager, taskbar, shell, editor, explorer and system applications |
| Language runtime | ChrisC, CLVM interpreter/JIT, ChrisAsm VM path |
| Native toolchain | ChrisAsm, ChrisO, ChrisLd and KCC |
| Networking | VirtIO-net and minimal in-kernel networking services |
| Virtualization | ChrisVM with the ChrisCPU emulator backend; ChrisHV remains an architectural target |
| Secondary architecture | RISC-V bring-up under QEMU <code>virt</code> |


## Architecture

~~~mermaid
flowchart TB
    FW["Firmware / Limine"] --> K["ChrisOS kernel"]
    K --> M["Metal: CPU, memory, interrupts, SMP"]
    K --> FS["Storage + ChrisFS"]
    K --> GFX["Graphics + window manager"]
    K --> NET["Networking"]
    K --> LANG["ChrisC / CLVM / JIT"]
    K --> TOOL["Native tools + shell"]

    FS --> DRV["ATA · AHCI · NVMe · VirtIO · USB"]
    GFX --> FB["Framebuffer / VirtIO-GPU / software 3D / VirGL"]
    LANG --> APPS["ChrisC applications"]
    TOOL --> NATIVE["ChrisAsm · ChrisO · ChrisLd · KCC"]

    subgraph "Virtual execution path"
      OS["ChrisOS"] --> VHW["Virtual hardware"]
      VHW --> VM["ChrisVM"]
      VM --> CPU["ChrisCPU emulator"]
      VM -. future .-> HV["ChrisHV VT-x/SVM"]
    end
~~~

This diagram is only a high-level orientation. Maintained architecture and subsystem documentation belongs on the [ChrisOS documentation site](https://os.christiansoftware.org/).

## Repository map

| Path | Purpose |
| --- | --- |
| <code>kernel/</code> | Kernel, hardware-facing code, filesystems, graphics, desktop, networking and kernel tools |
| <code>compiler/</code> | ChrisC, CLVM, JIT, debugger metadata, ChrisAsm/ChrisO/ChrisLd and KCC |
| <code>chrisvm/</code> | ChrisVM machine model, ChrisCPU emulator, devices, buses and tests |
| <code>APPS/</code> | ChrisC applications |
| <code>GAMES/</code> | Game/application workloads |
| <code>LIB/</code> | ChrisC libraries |
| <code>SYS/</code> | Guest-side system/toolchain content used to seed ChrisFS |
| <code>tools/</code> | Host utilities, generators and host-side tests |
| <code>tests/</code> | Additional test fixtures |
| <code>scripts/</code> | QEMU gates and developer scripts |
| <code>docs/</code> | Operational repository documentation only |
| <code>third_party/</code> | Vendored third-party components, including Limine |
| <code>iso_root/</code> | Source files used to stage the bootable ISO |
| <code>build/</code> | Generated artifacts; never source-controlled |

## Common workflows

| Goal | Command |
| --- | --- |
| Build bootable ISO | <code>make</code> or <code>make iso</code> |
| Build kernel only | <code>make kernel</code> |
| Create ChrisFS disk | <code>make disk.img</code> |
| Run interactive x86-64 ChrisOS | <code>make run</code> |
| Run VirGL configuration | <code>make run-virgl</code> |
| Run host test suite | <code>make host-gates</code> |
| Run QEMU integration gates | <code>make qemu-gates</code> |
| Run the broad local gate set | <code>make full-gates</code> |
| Build ChrisVM | <code>make chrisvm</code> |
| Test ChrisVM | <code>make chrisvm-test</code> |
| Build RISC-V bring-up | <code>make riscv</code> |
| Run RISC-V bring-up | <code>make run-riscv</code> |
| Seed the self-host workspace | <code>make seed-selfhost</code> |
| Clean generated output | <code>make clean</code> |

See [Testing](docs/development/testing.md) for the difference between host tests, QEMU integration gates and hardware evidence.

## Documentation

Repository-local:
- [Development environment](docs/getting-started/environment.md)
- [Build and run](docs/getting-started/build-and-run.md)
- [Development workflow](docs/development/workflow.md)
- [Testing](docs/development/testing.md)

Canonical technical documentation: **https://os.christiansoftware.org/**.

The repository does not maintain a second copy of architecture, subsystem, specification, status, audit, roadmap, ChrisVM/ChrisCPU, toolchain, graphics, filesystem, driver, hardware, research, or educational documentation.

## Development model

Changes to architecture, interfaces, subsystem behavior, capability status, or project direction should update `christianrss/chrisos_site`, not recreate those documents in this repository.

See [CONTRIBUTING.md](CONTRIBUTING.md) for the expected workflow.

## License

ChrisOS is distributed under the [MIT License](LICENSE).
