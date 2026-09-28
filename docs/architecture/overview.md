# Architecture overview

This document is the repository-level map of ChrisOS. It describes boundaries and data flow; subsystem documents contain detailed contracts.

## System layers

~~~mermaid
flowchart TB
    Firmware["BIOS / UEFI"] --> Limine["Limine"]
    Limine --> Kernel["ChrisOS kernel.elf"]

    Kernel --> Metal["kernel/metal"]
    Kernel --> Storage["kernel/fs"]
    Kernel --> Graphics["kernel/gfx"]
    Kernel --> WM["kernel/wm"]
    Kernel --> Network["kernel/net"]
    Kernel --> LangGlue["kernel/lang"]
    Kernel --> KernelTools["kernel/tools"]

    LangGlue --> Compilers["compiler/"]
    WM --> Apps["APPS/ + GAMES/"]
    Storage --> CFS["ChrisFS"]
    Storage --> Devices["ATA · AHCI · NVMe · VirtIO-blk · USB"]
    Graphics --> Display["Framebuffer · VirtIO-GPU · software 3D · VirGL"]
    Network --> VNet["VirtIO-net"]

    Compilers --> CLVM["ChrisC / CLVM / JIT"]
    Compilers --> Native["ChrisAsm / ChrisO / ChrisLd / KCC"]
~~~

## Boot and runtime flow

~~~mermaid
sequenceDiagram
    participant F as Firmware
    participant L as Limine
    participant K as kernel.elf
    participant D as Drivers
    participant C as ChrisFS
    participant W as Desktop
    participant A as Apps/CLVM

    F->>L: BIOS/UEFI boot
    L->>K: framebuffer, memory map, HHDM, boot metadata
    K->>K: CPU, memory, interrupts, timers, SMP
    K->>D: probe storage / graphics / network
    D->>C: expose block device
    C->>K: mount root workspace
    K->>W: initialize graphics and window manager
    W->>A: launch native/CLVM workloads
~~~

## Major source boundaries

- <code>kernel/metal/</code>: CPU/kernel machinery, memory, interrupts, SMP, processes, logging.
- <code>kernel/fs/</code>: block devices, partitions, storage drivers, ChrisFS, fsck and installation.
- <code>kernel/gfx/</code>: framebuffer, input, VirtIO-GPU/VirGL, shaders and software rendering.
- <code>kernel/wm/</code>: desktop, task/window model and UI composition.
- <code>kernel/net/</code>: VirtIO networking and minimal network services.
- <code>kernel/lang/</code> + <code>compiler/</code>: ChrisC, CLVM, JIT, debugging and native toolchain.
- <code>chrisvm/</code>: machine model, buses, devices, ChrisCPU and ChrisHV boundary.

## Virtualization direction

~~~mermaid
flowchart TB
    OS["ChrisOS"] --> Drivers["Chris drivers"]
    Drivers --> VHW["Virtual hardware"]
    VHW --> VM["ChrisVM"]
    VM --> CPU["ChrisCPU emulator"]
    VM -. future .-> HV["ChrisHV VT-x/SVM"]
    CPU --> HW["Host hardware"]
    HV --> HW
~~~

QEMU remains the primary complete integration environment today. ChrisVM should evolve behind the same virtual-hardware contract instead of forcing ChrisOS to distinguish ChrisCPU from ChrisHV.

## Build graph

~~~mermaid
flowchart LR
    Src["kernel/ + compiler/"] --> Obj["build/obj/*.o"]
    Obj --> ELF["build/iso/boot/kernel.elf"]
    LimineSrc["iso_root/ + third_party/limine"] --> ISOStage["build/iso/"]
    ELF --> ISOStage
    ISOStage --> ISO["build/os.iso"]

    HostTools["tools/"] --> HostBin["build/host/"]
    HostBin --> Disk["build/disk.img"]
    GuestContent["SYS/ APPS/ LIB/ GAMES/ SRC/"] --> Disk

    VMsrc["chrisvm/"] --> VMbin["build/chrisvm/chrisvm"]
~~~

## Architecture documents

- [Graphics architecture](../GRAPHICS_ARCHITECTURE.md)
- [Developer toolkit architecture](../DEVTOOL_ARCHITECTURE.md)
- [ChrisVM specification](../chrisvm-spec-v1.md)
- [ChrisVM boot protocol](../chrisvm-boot-protocol.md)
- [Resource ownership](../RESOURCE_OWNERSHIP.md)
- [Locking](../LOCKING.md)
- [Chris kernel C profile](../CHRIS_KERNEL_C_PROFILE.md)
- [ChrisO format](../CHRISO_FORMAT.md)

Architecture and status are separate. Before describing a subsystem as supported, consult [CURRENT_CAPABILITIES.md](../CURRENT_CAPABILITIES.md), [HARDWARE_COMPATIBILITY.md](../HARDWARE_COMPATIBILITY.md) and the relevant gate.
