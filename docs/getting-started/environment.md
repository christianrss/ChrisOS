# Development environment

This guide describes the reference host environment used to build and test ChrisOS. The canonical, expanded setup documentation is the [Developer Guide](https://os.christiansoftware.org/en/17-developer-guide/), including dedicated [Linux](https://os.christiansoftware.org/en/17-developer-guide/linux-development-environment/) and [Windows/WSL2](https://os.christiansoftware.org/en/17-developer-guide/windows-wsl-development-environment/) paths.

## Reference platform

The lowest-friction path is a recent **x86-64 Debian or Ubuntu Linux** installation.

The top-level build assumes GNU-style tools. The interactive <code>make run</code> target explicitly requests KVM. WSL can be useful for compilation, but interactive virtualization depends on KVM availability in that environment.

## Required tools

- GNU Make
- GCC
- GNU ld/binutils
- NASM
- xorriso
- QEMU x86-64
- Python 3
- Git

Debian/Ubuntu baseline:

~~~bash
sudo apt update
sudo apt install \
  build-essential \
  binutils \
  nasm \
  xorriso \
  qemu-system-x86 \
  qemu-utils \
  python3 \
  git
~~~

The installer integration gate also expects OVMF:

~~~bash
sudo apt install ovmf
~~~

RISC-V bring-up additionally needs a package that provides <code>qemu-system-riscv64</code>. Package names vary by distribution.

ChrisVM can use SDL when <code>sdl2-config</code> is present, but the headless tests do not require SDL.

## Verify the environment

~~~bash
./scripts/check-dev-env.sh
~~~

The script separates required tools from optional capabilities and reports whether <code>/dev/kvm</code> is usable.

A missing KVM device does not prevent host tests or normal headless QEMU gates, because those gates use TCG. It does prevent the current <code>make run</code> and <code>make run-virgl</code> recipes from working unchanged.

## Repository checkout

~~~bash
git clone https://github.com/christianrss/ChrisOS.git
cd ChrisOS
git status
~~~

Avoid committing anything under <code>build/</code>.

## Generated artifacts

| Path | Contents |
| --- | --- |
| <code>build/obj/</code> | kernel/compiler object files |
| <code>build/iso/</code> | staged ISO filesystem |
| <code>build/os.iso</code> | bootable ChrisOS image |
| <code>build/disk.img</code> | ChrisFS workspace disk |
| <code>build/host/</code> | host utilities and tests |
| <code>build/user/</code> | user-mode test binaries |
| <code>build/chrisvm/</code> | ChrisVM executable, tests and guest fixtures |

Use <code>make clean</code> to remove generated output.

## Optional capabilities

### KVM

~~~bash
test -r /dev/kvm -a -w /dev/kvm && echo "KVM usable" || echo "KVM unavailable"
~~~

### VirGL/OpenGL

VirGL testing depends on the host QEMU build, an OpenGL-capable display backend and, on some hosts, a usable DRM render node. <code>make test-qemu-virgl</code> detects unsupported hosts and skips instead of treating absence of host VirGL as a guest failure.

### RISC-V

<code>make riscv</code> uses Clang/LLD with a RISC-V target. <code>make run-riscv</code> additionally requires <code>qemu-system-riscv64</code>.

## Before opening a build issue

~~~bash
./scripts/check-dev-env.sh
gcc --version
ld --version | head -n 1
nasm -v
qemu-system-x86_64 --version | head -n 1
git rev-parse HEAD
~~~

Include the exact make target and the first relevant error, not only the final make exit code.
