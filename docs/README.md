# ChrisOS repository documentation

This directory contains the documentation that must stay close to the source tree: developer setup, build/run procedures, architecture contracts, implementation status, test evidence and focused subsystem notes.

The long-form educational documentation is published at **https://os.christiansoftware.org/** and maintained in the <code>christianrss/chrisos_site</code> repository.

## Start here

### Build or run ChrisOS

1. [Development environment](getting-started/environment.md)
2. [Build and run](getting-started/build-and-run.md)
3. [Installation behavior](INSTALLATION.md)

### Understand the architecture

1. [Architecture overview](architecture/overview.md)
2. [Current capabilities](CURRENT_CAPABILITIES.md)
3. [Hardware compatibility](HARDWARE_COMPATIBILITY.md)
4. [Graphics architecture](GRAPHICS_ARCHITECTURE.md)
5. [Developer toolkit architecture](DEVTOOL_ARCHITECTURE.md)
6. [ChrisVM specification](chrisvm-spec-v1.md)
7. [ChrisVM boot protocol](chrisvm-boot-protocol.md)

### Contribute code

1. [Contributing guide](../CONTRIBUTING.md)
2. [Development workflow](development/workflow.md)
3. [Testing and evidence](development/testing.md)

## Documentation classes

| Class | Meaning | Examples |
| --- | --- | --- |
| Guide | Stable instructions for using/developing the current tree | <code>getting-started/*</code>, <code>development/*</code> |
| Architecture | Contracts and component boundaries | <code>GRAPHICS_ARCHITECTURE.md</code>, <code>DEVTOOL_ARCHITECTURE.md</code> |
| Status | Snapshot of what is implemented/proven at a point in time | <code>CURRENT_CAPABILITIES.md</code>, <code>SELFHOST_STATUS.md</code> |
| Audit | Evidence from a focused engineering campaign | <code>STABILITY_AUDIT.md</code>, <code>CURRENT_*_AUDIT.md</code> |
| Plan | Future work; not a current capability claim | <code>REAL_HARDWARE_PLAN.md</code>, <code>KERNEL_SELFHOST_PLAN.md</code> |
| Specification | Formats, ABIs or protocols consumed by code | <code>CHRISO_FORMAT.md</code>, <code>CLVM_ABI.md</code> |

Do not turn plans into current-state claims. Do not silently rewrite an audit to describe newer code; update the current-state layer and preserve historical evidence.

## Topic index

### Kernel, memory and concurrency
- [Current kernel audit](CURRENT_KERNEL_AUDIT.md)
- [Locking](LOCKING.md)
- [Resource ownership](RESOURCE_OWNERSHIP.md)
- [Stability audit](STABILITY_AUDIT.md)
- [Stability report](STABILITY_REPORT.md)
- [Foundation audit](FOUNDATION_AUDIT.md)

### Hardware and installation
- [Hardware bring-up](HARDWARE_BRINGUP.md)
- [Hardware compatibility](HARDWARE_COMPATIBILITY.md)
- [Current hardware audit](CURRENT_HARDWARE_AUDIT.md)
- [Installation](INSTALLATION.md)
- [Real hardware plan](REAL_HARDWARE_PLAN.md)

### Graphics
- [Graphics architecture](GRAPHICS_ARCHITECTURE.md)
- [Current graphics audit](CURRENT_GRAPHICS_AUDIT.md)
- [GFX2D](GFX2D.md)
- [GFX3D](GFX3D.md)
- [VirtIO-GPU](VIRTIO_GPU.md)
- [VirGL](VIRGL.md)
- [Shader architecture](SHADER_ARCHITECTURE.md)
- [GLSL support](GLSL_SUPPORT.md)

### Languages, toolchain and self-hosting
- [Developer toolkit architecture](DEVTOOL_ARCHITECTURE.md)
- [Developer toolkit audit](DEVTOOL_AUDIT.md)
- [Self-host status](SELFHOST_STATUS.md)
- [ChrisC self-host conformance](CHRISC_SELFHOST_CONFORMANCE.md)
- [Native toolchain audit](NATIVE_TOOLCHAIN_AUDIT.md)
- [Native toolchain gap](NATIVE_TOOLCHAIN_GAP.md)
- [Kernel C profile](CHRIS_KERNEL_C_PROFILE.md)
- [KCC status](KCC_STATUS.md)
- [ChrisAsm status](CHRISASM_STATUS.md)
- [ChrisO format](CHRISO_FORMAT.md)
- [ChrisLd status](CHRISLD_STATUS.md)
- [Kernel self-host plan](KERNEL_SELFHOST_PLAN.md)

### ChrisVM
- [ChrisVM v1 specification](chrisvm-spec-v1.md)
- [ChrisVM boot protocol](chrisvm-boot-protocol.md)
- [ChrisVM subsystem notes](chrisvm/)

## Documentation rules

- Commands in guides must be runnable from the repository root unless explicitly stated otherwise.
- Capability statements must say what evidence exists: source only, host-tested, QEMU-tested or hardware-tested.
- Generated output belongs under <code>build/</code>.
- A code change that modifies a public contract, build procedure or capability claim should update the relevant documentation in the same pull request.
- Repository-local documents are authoritative for commands and contracts tied to the current source tree.
